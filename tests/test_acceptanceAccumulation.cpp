// =============================================================================
// Accountant's examples — an accumulation register of balances.
//
// Expected figures: tools/oracle/acceptance.py (tests/fixtures/acceptance/accumulation.json).
// The register is the platform's own: ContributeTables declares the totals, the
// triggers keep them, and the balance is read through the Balance, Turnovers and
// BalanceAndTurnovers queryables. Movements are written and deleted through the
// same door the record set uses. A line stored inactive is not an unposted document:
// unposting deletes the document's lines.
//
// A bare date is still Including on the platform, and a BalanceAndTurnovers opening
// still includes the point the period starts on. The accountant's answer excludes
// that point. That behaviour change is a separate platform change and is not made
// here; both cases stay known gaps until it lands.
// =============================================================================

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <wx/filename.h>
#include <wx/init.h>

#include <nlohmann/json.hpp>

#include "backend/appData.h"
#include "backend/compiler/compileCode.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseMaterializeBuilder.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/dimension/metaDimensionObject.h"
#include "backend/metaCollection/resource/metaResourceObject.h"
#include "backend/metaCollection/partial/accumulationRegister.h"
#include "backend/metaCollection/partial/accumulationRegisterEnum.h"
#include "backend/metaCollection/partial/registerQueryLowering.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/query/columnLayout.h"
#include "backend/query/dataQueryBuilder.h"
#include "backend/query/schemaSnapshot.h"
#include "backend/system/value/valueBoundary.h"
#include "core/clsid.h"

namespace {

nlohmann::json LoadFixture(const ibString& fileName)
{
	wxFileName here(ibString::FromUTF8(__FILE__));
	here.SetFullName(wxEmptyString);
	here.AppendDir(wxT("fixtures"));
	here.AppendDir(wxT("acceptance"));
	here.SetFullName(fileName);
	std::ifstream in(std::string(here.GetFullPath().utf8_str()));
	EXPECT_TRUE(in.good()) << here.GetFullPath();
	return nlohmann::json::parse(in);
}

ibString AsWx(const nlohmann::json& node)
{
	return ibString::FromUTF8(node.get<std::string>());
}

ibDateTime At(const nlohmann::json& node)
{
	int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
	const std::string text = node.get<std::string>();
	if (std::sscanf(text.c_str(), "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6) {
		ADD_FAILURE() << "malformed date: " << text;
		return ibDateTime();
	}
	return ibDateTime(year, static_cast<unsigned>(month), static_cast<unsigned>(day),
		static_cast<unsigned>(hour), static_cast<unsigned>(minute), static_cast<unsigned>(second));
}

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

struct Movement {
	ibString doc;
	int line = 0;
	ibDateTime when;
	ibString wh;
	ibString item;
	bool receipt = false;
	ibString qty;
	bool active = true;
};

// What the lowering folds a projection through (ProjectionHandle, queryLowering.cpp): a column named by the
// projection in the statement's spelling, with an id of its own.
class ProjectionHandle : public ibBackendColumnRawDB {
public:
	ProjectionHandle(const ibString& alias, ibMetaID id)
		: ibBackendColumnRawDB(ibSqlAliasOf(alias), RawType::Number), m_id(SyntheticId(SyntheticKind::Output, id)) {}

	ibMetaID GetColumnId() const override { return m_id; }

private:
	ibMetaID m_id;
};

struct AcceptanceAccumulation : ::testing::Test {
	wxInitializer m_wxInit;
	// RunDatabase sets this thread's code style to the configuration's. The binary runs VES
	// (test_compiler.cpp), so TearDown puts it back, as StyleGuard does.
	const short m_codeStyle = ibCompileCode::GetCodeStyle();
	std::shared_ptr<ibDatabaseLayerSQLite> db;
	ibString dbPath;
	std::unique_ptr<ibMetaDataConfigurationFile> cfg;
	ibValueMetaObject* document = nullptr;
	ibValueMetaObjectAccumulationRegister* reg = nullptr;
	ibValueMetaObjectDimension* warehouse = nullptr;
	ibValueMetaObjectDimension* item = nullptr;
	ibMaterializeSpec readSpec;
	ibMaterializeView turnovers;
	ibString movementsTable;
	ibString receiptAlias;
	ibString expenseAlias;
	ibString turnoverAlias;
	std::vector<ibString> keyColumns;
	std::vector<ibString> warehouseFields;
	std::vector<ibString> itemFields;
	nlohmann::json fixture;
	std::map<ibString, ibValue> recorders;
	std::vector<Movement> posted;
	bool ready = false;

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";
		if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
			GTEST_SKIP() << "appData env unavailable headless";
		ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
		if (pool == nullptr)
			GTEST_SKIP() << "no connection pool after CreateAppDataEnv";
		db = std::make_shared<ibDatabaseLayerSQLite>();
		// A file, so a second connection checked out of the pool sees the same rows.
		// A clone of :memory: would be an empty database.
		dbPath = wxFileName::CreateTempFileName(wxT("oesacc"));
		if (!db->Open(dbPath))
			GTEST_SKIP() << "SQLite open failed";
		pool->Init(db, /*maxSize=*/4, /*minIdle=*/0);

		fixture = LoadFixture(wxT("accumulation.json"));
		cfg = std::make_unique<ibMetaDataConfigurationFile>();
		ibValueMetaObjectConfiguration* root = cfg->GetCommonMetaObject();
		ASSERT_NE(root, nullptr);
		document = cfg->CreateMetaObject(g_metaDocumentCLSID, root, false);
		reg = dynamic_cast<ibValueMetaObjectAccumulationRegister*>(
			cfg->CreateMetaObject(g_metaAccumulationRegisterCLSID, root, false));
		ASSERT_NE(document, nullptr);
		ASSERT_NE(reg, nullptr);
		reg->SetName(wxT("Stock"));
		warehouse = dynamic_cast<ibValueMetaObjectDimension*>(cfg->CreateMetaObject(g_metaDimensionCLSID, reg, false));
		item = dynamic_cast<ibValueMetaObjectDimension*>(cfg->CreateMetaObject(g_metaDimensionCLSID, reg, false));
		auto* resource = dynamic_cast<ibValueMetaObjectResource*>(cfg->CreateMetaObject(g_metaResourceCLSID, reg, false));
		ASSERT_NE(warehouse, nullptr);
		ASSERT_NE(item, nullptr);
		ASSERT_NE(resource, nullptr);
		warehouse->SetName(wxT("Warehouse"));
		item->SetName(wxT("Item"));
		resource->SetName(wxT("Quantity"));
		warehouse->GetTypeDesc().SetDefaultMetaType(ibValueTypes::TYPE_STRING);
		item->GetTypeDesc().SetDefaultMetaType(ibValueTypes::TYPE_STRING);

		reg->GetRegisterRecorder()->GetTypeDesc().AppendMetaType(
			reference_to_clsid(document->GetMetaID(), clsid_metaclass(document->GetClassType())));
		ASSERT_TRUE(cfg->RunDatabase());
		ASSERT_TRUE(reg->HasMovementArm());
		ASSERT_EQ(reg->GetTotalsPeriodUnit(), ibTotalsPeriod::Day);

		ibSchemaSnapshot snapshot;
		reg->ContributeTables(snapshot);
		for (const ibSchemaTable& table : snapshot.Tables())
			CreateTable(table);

		const ibSchemaTable* totals = nullptr;
		for (const ibSchemaTable& table : snapshot.Tables())
			if (table.m_derived && table.m_name == reg->GetRegisterTableNameDB())
				totals = &table;
		ASSERT_NE(totals, nullptr);
		const ibMaterializeSpec render = totals->m_materialize.ToRenderSpec(totals->m_name);
		readSpec = totals->m_materialize.ToReadSpec(totals->m_name);
		movementsTable = render.m_source;
		ASSERT_FALSE(movementsTable.IsEmpty());

		const ibMaterializeSql sql = RenderMaterialization(render,
			&ibDatabaseLayerSQLite::MaterializationDialect(), ibDatabaseLayerSQLite::Dialect());
		ASSERT_TRUE(sql.Apply(*db)) << "the register's totals were not installed";

		bool foundView = false;
		for (const ibMaterializeView& view : readSpec.m_views) {
			if (view.m_name == reg->GetTurnoverViewName()) {
				turnovers = view;
				foundView = true;
			}
		}
		ASSERT_TRUE(foundView);
		ASSERT_TRUE(turnovers.m_withMovements);
		for (const ibMaterializeViewColumn& column : turnovers.m_columns) {
			if (column.m_alias.EndsWith(wxT("_Receipt"))) receiptAlias = column.m_alias;
			else if (column.m_alias.EndsWith(wxT("_Expense"))) expenseAlias = column.m_alias;
			else if (column.m_alias.EndsWith(wxT("_Turnover"))) turnoverAlias = column.m_alias;
		}
		ASSERT_FALSE(receiptAlias.IsEmpty());
		ASSERT_FALSE(expenseAlias.IsEmpty());
		ASSERT_FALSE(turnoverAlias.IsEmpty());
		ASSERT_FALSE(turnovers.m_movementColumns.empty());

		const std::vector<ibString> periodFields = ColumnFieldNames(reg->GetRegisterPeriod()->GetQueryColumn());
		for (const ibString& field : readSpec.m_keyColumns)
			if (std::find(periodFields.begin(), periodFields.end(), field) == periodFields.end())
				keyColumns.push_back(field);
		const auto keepKeyed = [&](const std::vector<ibString>& fields) {
			std::vector<ibString> kept;
			for (const ibString& field : fields)
				if (std::find(keyColumns.begin(), keyColumns.end(), field) != keyColumns.end())
					kept.push_back(field);
			return kept;
		};
		warehouseFields = keepKeyed(ColumnFieldNames(warehouse->GetQueryColumn()));
		itemFields = keepKeyed(ColumnFieldNames(item->GetQueryColumn()));
		ASSERT_FALSE(warehouseFields.empty());
		ASSERT_FALSE(itemFields.empty());
		ready = true;
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

	void Assign(std::vector<ibDmlAssign>& out, const ibBackendQueryColumn* column, const ibValue& value) const {
		const std::vector<ibString> fields = ColumnFieldNames(column);
		ibQueryStatement capture(ibQueryStatement::Kind::Delete, ibString(), fields);
		int position = 1;
		ibColumnCodec::WriteValue(column, cfg.get(), value, &capture, position);
		const std::vector<ibQueryExprPtr>& consts = capture.CapturedValues();
		for (size_t i = 0; i < fields.size(); ++i)
			out.push_back(ibDmlAssign{ fields[i], (i < consts.size() && consts[i]) ? consts[i] : ibConst(ibValue()) });
	}

	const ibValue& Recorder(const ibString& doc) {
		const auto found = recorders.find(doc);
		if (found != recorders.end())
			return found->second;
		recorders[doc] = ibValue(ibValueReferenceDataObject::Create(
			cfg.get(), document->GetMetaID(), ibGuid(ibGuid::newGuid())));
		return recorders[doc];
	}

	// The door WriteRecordSet's SaveData stands on: one row, by column, into the register.
	void Insert(const Movement& movement) {
		const ibValue kind = ibValue::CreateEnumObject<ibValueEnumAccumulationRegisterRecordType>(
			movement.receipt ? ibRecordType::eReceipt : ibRecordType::eExpense);
		ibDataQueryBuilder q(ibConnectionPool::ThreadHolder());
		q.From(reg->GetQueryable());
		q.SetValue(reg->GetRegisterActive()->GetQueryColumn(), ibValue(movement.active));
		q.SetValue(reg->GetRegisterPeriod()->GetQueryColumn(), ibValue(movement.when));
		q.SetValue(reg->GetRegisterRecorder()->GetQueryColumn(), Recorder(movement.doc));
		q.SetValue(reg->GetRegisterLineNumber()->GetQueryColumn(), ibValue(ibNumber(movement.line)));
		q.SetValue(reg->GetRegisterRecordType()->GetQueryColumn(), kind);
		q.SetValue(warehouse->GetQueryColumn(), ibValue(movement.wh));
		q.SetValue(item->GetQueryColumn(), ibValue(movement.item));
		q.SetValue(reg->GetResourceArrayObject().front()->GetQueryColumn(), ibValue(ibNumber(movement.qty)));
		ASSERT_TRUE(q.Insert()) << movement.doc << " " << movement.line;
	}

	// The door DeleteRecordSet stands on. Unposting deletes the document's lines.
	void DeleteLine(const ibString& doc, int line) {
		ibDataQueryBuilder q(ibConnectionPool::ThreadHolder());
		q.From(reg->GetQueryable());
		q.Where(reg->GetRegisterRecorder()->GetQueryColumn(), Recorder(doc));
		q.Where(reg->GetRegisterLineNumber()->GetQueryColumn(), ibValue(ibNumber(line)));
		ASSERT_TRUE(q.Delete());
	}

	Movement FromJson(const nlohmann::json& node) const {
		Movement movement;
		movement.doc = AsWx(node["doc"]);
		movement.line = node["line"].get<int>();
		movement.when = At(node["when"]);
		movement.wh = AsWx(node["wh"]);
		movement.item = AsWx(node["item"]);
		movement.receipt = node["receipt"].get<bool>();
		movement.qty = AsWx(node["qty"]);
		movement.active = node["active"].get<bool>();
		return movement;
	}

	void PostOpening() {
		posted.clear();
		for (const auto& node : fixture["movements"]) {
			posted.push_back(FromJson(node));
			Insert(posted.back());
		}
	}

	ibValue Position(const ibDateTime& moment, const std::string& road) const {
		if (road == "bare")
			return ibValue(moment);
		const ibBoundaryKind kind = road == "excluding" ? ibBoundaryKind_Excluding : ibBoundaryKind_Including;
		return ibValue(new ibValueBoundary(ibValue(moment), kind));
	}

	const ibBackendQueryColumn* MustColumn(const ibBackendQueryable* source, const ibString& name) const {
		const ibBackendQueryColumn* col = source != nullptr ? source->ResolveColumnByName(name) : nullptr;
		EXPECT_NE(col, nullptr) << name;
		return col;
	}

	ibNumber FigureOf(const ibBackendQueryable* source, const ibBackendQueryColumn* figure,
		const ibString& wh, const ibString& itemCode) {
		if (source == nullptr || figure == nullptr)
			return ibNumber();
		const ibBackendQueryColumn* whCol = MustColumn(source, wxT("Warehouse"));
		const ibBackendQueryColumn* itemCol = MustColumn(source, wxT("Item"));
		if (whCol == nullptr || itemCol == nullptr)
			return ibNumber();
		ibDataQueryBuilder q(ibConnectionPool::ThreadHolder());
		q.From(source);
		q.Select(figure, wxT("fig"));
		q.Select(whCol, wxT("wh"));
		q.Select(itemCol, wxT("item"));
		ibDataQueryResult rows = q.Execute(ibReadPageRequest{});
		while (rows.Next()) {
			if (rows.GetValue(whCol).GetString() == wh && rows.GetValue(itemCol).GetString() == itemCode)
				return rows.GetValue(figure).GetNumber();
		}
		return ibNumber();
	}

	void ExpectNear(const ibNumber& got, const ibString& want, const ibString& where) const {
		EXPECT_EQ(0, got.Compare(ibNumber(want))) << where << " got " << got.ToString() << " want " << want;
	}

	ibNumber BalanceOf(const nlohmann::json& question) {
		const std::string road = question["road"].get<std::string>();
		ibBalanceQueryable view(reg, Position(At(question["at"]), road));
		return FigureOf(&view, MustColumn(&view, wxT("QuantityBalance")),
			AsWx(question["wh"]), AsWx(question["item"]));
	}

	void ExpectBalance(const nlohmann::json& question) {
		ASSERT_EQ(question["kind"], "balance");
		const ibNumber got = BalanceOf(question);
		ExpectNear(got, AsWx(question["qty"]), AsWx(question["wh"]) + wxT(" ") + AsWx(question["item"])
			+ wxT(" ") + ibString::FromUTF8(question["road"].get<std::string>()));
	}

	void ExpectBalanceAndTurnovers(const nlohmann::json& question) {
		const ibValue from = Position(At(question["from"]), "excluding");
		const ibValue to = Position(At(question["to"]), "excluding");
		ibBalanceAndTurnoverQueryable view(reg, from, to);
		const ibString wh = AsWx(question["wh"]);
		const ibString itemCode = AsWx(question["item"]);
		const ibString where = wh + wxT(" ") + itemCode;
		ExpectNear(FigureOf(&view, MustColumn(&view, wxT("QuantityOpeningBalance")), wh, itemCode),
			AsWx(question["opening"]), where + wxT(" opening"));
		ExpectNear(FigureOf(&view, MustColumn(&view, wxT("QuantityReceipt")), wh, itemCode),
			AsWx(question["receipt"]), where + wxT(" receipt"));
		ExpectNear(FigureOf(&view, MustColumn(&view, wxT("QuantityExpense")), wh, itemCode),
			AsWx(question["expense"]), where + wxT(" expense"));
		ExpectNear(FigureOf(&view, MustColumn(&view, wxT("QuantityClosingBalance")), wh, itemCode),
			AsWx(question["closing"]), where + wxT(" closing"));
	}

	void ApplyRepostAndUnpost() {
		const auto& repost = fixture["repost"];
		DeleteLine(AsWx(repost["doc"]), repost["line"].get<int>());
		Movement again;
		for (const Movement& movement : posted)
			if (movement.doc == AsWx(repost["doc"]) && movement.line == repost["line"].get<int>())
				again = movement;
		again.qty = AsWx(repost["qty"]);
		again.receipt = repost["receipt"].get<bool>();
		Insert(again);
		ExpectBalance(repost["then"]);

		const auto& unpost = fixture["unpost"];
		DeleteLine(AsWx(unpost["doc"]), unpost["line"].get<int>());
		ExpectBalance(unpost["then"]);
	}
};

} // namespace

TEST_F(AcceptanceAccumulation, TheBalanceAtMidnightLeavesOutTheDayThatStarts)
{
	if (!ready) return;
	PostOpening();
	for (const auto& question : fixture["balances"]) {
		if (question["road"] == "bare")
			continue;
		ExpectBalance(question);
	}
}

// Disabled, not skipped on a mismatch: a known gap — a bare date is still Including, and the
// accountant wants the balance before that point. Enabled by the change that closes it.
TEST_F(AcceptanceAccumulation, DISABLED_ABareDateIsTheBalanceBeforeThatPoint)
{
	if (!ready) return;
	PostOpening();
	for (const auto& question : fixture["balances"]) {
		if (question["road"] != "bare")
			continue;
		const ibNumber got = BalanceOf(question);
		const ibNumber want(AsWx(question["qty"]));
		EXPECT_EQ(0, got.Compare(want)) << "got " << got.ToString() << ", the accountant wants " << want.ToString();
	}
}

TEST_F(AcceptanceAccumulation, TurnoversRunFromMidnightUpToTheNext)
{
	if (!ready) return;
	PostOpening();
	for (const auto& question : fixture["turnovers"]) {
		const ibValue from = Position(At(question["from"]), "including");
		const ibValue to = Position(At(question["to"]), "excluding");
		ibTurnoverQueryable view(reg, from, to);
		const ibBackendQueryColumn* receiptCol = MustColumn(&view, wxT("QuantityReceipt"));
		const ibBackendQueryColumn* expenseCol = MustColumn(&view, wxT("QuantityExpense"));
		const ibString wh = AsWx(question["wh"]);
		const ibString itemCode = AsWx(question["item"]);
		ExpectNear(FigureOf(&view, receiptCol, wh, itemCode), AsWx(question["receipt"]), wh + wxT(" receipt"));
		ExpectNear(FigureOf(&view, expenseCol, wh, itemCode), AsWx(question["expense"]), wh + wxT(" expense"));
	}
}

TEST_F(AcceptanceAccumulation, BalanceAndTurnoversIsTheOpeningPlusTheMonth)
{
	if (!ready) return;
	PostOpening();
	for (const auto& question : fixture["balance_and_turnovers"]) {
		// A period that opens on a posted movement is the start-point case below.
		if (question["from"] != "2026-03-01 00:00:00")
			continue;
		ExpectBalanceAndTurnovers(question);
	}
}

// Disabled for the same known gap: the opening still includes the point the period starts on.
TEST_F(AcceptanceAccumulation, DISABLED_TheOpeningLeavesOutThePointThePeriodStartsOn)
{
	if (!ready) return;
	PostOpening();
	for (const auto& question : fixture["balance_and_turnovers"]) {
		if (question["from"] == "2026-03-01 00:00:00")
			continue;
		const ibValue from = Position(At(question["from"]), "excluding");
		const ibValue to = Position(At(question["to"]), "excluding");
		ibBalanceAndTurnoverQueryable view(reg, from, to);
		const ibString wh = AsWx(question["wh"]);
		const ibString itemCode = AsWx(question["item"]);
		const ibNumber opening = FigureOf(&view, MustColumn(&view, wxT("QuantityOpeningBalance")), wh, itemCode);
		const ibNumber receipt = FigureOf(&view, MustColumn(&view, wxT("QuantityReceipt")), wh, itemCode);
		const ibNumber wantOpening(AsWx(question["opening"]));
		const ibNumber wantReceipt(AsWx(question["receipt"]));
		EXPECT_EQ(0, opening.Compare(wantOpening)) << "opening got " << opening.ToString()
			<< ", the accountant wants " << wantOpening.ToString();
		EXPECT_EQ(0, receipt.Compare(wantReceipt)) << "receipt got " << receipt.ToString()
			<< ", the accountant wants " << wantReceipt.ToString();
		ExpectBalanceAndTurnovers(question);
	}
}

TEST_F(AcceptanceAccumulation, DayAndMonthReadingsSpanMarchIntoApril)
{
	if (!ready) return;
	PostOpening();
	for (const auto& question : fixture["periods"]) {
		ibRegFold fold;
		fold.m_kind = ibRegGranularity::Calendar;
		fold.m_unit = question["unit"] == "day" ? ibTotalsPeriod::Day : ibTotalsPeriod::Month;
		const ibValue from = Position(At(question["from"]), "including");
		const ibValue to = Position(At(question["to"]), "excluding");
		ibTurnoverQueryable view(reg, from, to, nullptr, fold);
		const ibBackendQueryColumn* receiptCol = MustColumn(&view, wxT("QuantityReceipt"));
		const ibBackendQueryColumn* expenseCol = MustColumn(&view, wxT("QuantityExpense"));
		const ibBackendQueryColumn* whCol = MustColumn(&view, wxT("Warehouse"));
		const ibBackendQueryColumn* itemCol = MustColumn(&view, wxT("Item"));
		ASSERT_NE(receiptCol, nullptr);
		ibDataQueryBuilder q(ibConnectionPool::ThreadHolder());
		q.From(&view);
		q.Select(receiptCol, wxT("in"));
		q.Select(expenseCol, wxT("out"));
		if (whCol != nullptr) q.Select(whCol, wxT("wh"));
		if (itemCol != nullptr) q.Select(itemCol, wxT("item"));
		ibNumber receipt;
		ibNumber expense;
		int rows = 0;
		ibDataQueryResult got = q.Execute(ibReadPageRequest{});
		while (got.Next()) {
			if (whCol != nullptr && got.GetValue(whCol).GetString() != AsWx(question["wh"]))
				continue;
			if (itemCol != nullptr && got.GetValue(itemCol).GetString() != AsWx(question["item"]))
				continue;
			receipt = receipt + got.GetValue(receiptCol).GetNumber();
			expense = expense + got.GetValue(expenseCol).GetNumber();
			++rows;
		}
		const ibString unit = ibString::FromUTF8(question["unit"].get<std::string>());
		EXPECT_EQ(question["rows"].get<int>(), rows) << unit;
		ExpectNear(receipt, AsWx(question["receipt"]), unit + wxT(" receipt"));
		ExpectNear(expense, AsWx(question["expense"]), unit + wxT(" expense"));
	}
}

// ⭐ BY THE RECORDER THE SERVER ANSWERS WHAT THE RAM ROAD ANSWERS (#646). The turnovers and the balance-and-turnovers
// by Recorder or Record were computed in memory only; they now stand on the totals — a row per movement, each opening
// where the one before it closed, a key carried in and untouched at the interval's beginning — and the RAM road, which
// reads the movements themselves, is their oracle. From the 1st, with the line out of force inside the interval (it
// moved nothing on either road — the RAM one counted it, ibRegWhereActive), and from the 2nd, so what the 1st left is
// carried in.
TEST_F(AcceptanceAccumulation, ByTheRecorderTheServerAnswersAsTheRamRoad)
{
	if (!ready) return;
	PostOpening();
	const ibValue to(ibDateTime(2026, 4, 30, 23, 59, 59));

	// A row: its key as text, its figures as numbers. The two roads answer in orders of their own, so each list is
	// sorted before they are compared.
	struct Row {
		ibString key;
		std::vector<ibNumber> figures;
	};
	const auto sorted = [](std::vector<Row> rows) {
		std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
			if (a.key != b.key)
				return a.key < b.key;
			for (size_t i = 0; i < a.figures.size() && i < b.figures.size(); ++i)
				if (const int c = a.figures[i].Compare(b.figures[i]); c != 0)
					return c < 0;
			return false;
		});
		return rows;
	};
	const auto served = [&](const ibBackendQueryable& view, const std::vector<ibString>& keys, const std::vector<ibString>& figures) {
		std::vector<const ibBackendQueryColumn*> keyCols, figureCols;
		for (const ibString& name : keys)
			keyCols.push_back(MustColumn(&view, name));
		for (const ibString& name : figures)
			figureCols.push_back(MustColumn(&view, name));
		ibDataQueryBuilder q(ibConnectionPool::ThreadHolder());
		q.From(&view);
		unsigned n = 0;
		for (const ibBackendQueryColumn* column : keyCols)
			if (column != nullptr) q.Select(column, ibString::Format(wxT("k%u"), n++));
		for (const ibBackendQueryColumn* column : figureCols)
			if (column != nullptr) q.Select(column, ibString::Format(wxT("f%u"), n++));
		std::vector<Row> rows;
		ibDataQueryResult got = q.Execute(ibReadPageRequest{});
		while (got.Next()) {
			Row row;
			for (const ibBackendQueryColumn* column : keyCols)
				row.key << (column != nullptr ? got.GetValue(column).GetString() : ibString()) << wxT("|");
			for (const ibBackendQueryColumn* column : figureCols)
				row.figures.push_back(column != nullptr ? got.GetValue(column).GetNumber() : ibNumber());
			rows.push_back(std::move(row));
		}
		return sorted(std::move(rows));
	};
	const auto computed = [&](const ibQueryRamTable& table, const std::vector<ibString>& keys, const std::vector<ibString>& figures) {
		std::vector<Row> rows;
		for (long r = 0; r < table.RowCount(); ++r) {
			Row row;
			for (const ibString& name : keys)
				row.key << table.GetCell(r, table.ColumnIdByName(name)).GetString() << wxT("|");
			for (const ibString& name : figures)
				row.figures.push_back(table.GetCell(r, table.ColumnIdByName(name)).GetNumber());
			rows.push_back(std::move(row));
		}
		return sorted(std::move(rows));
	};
	const auto expectSame = [](const std::vector<Row>& server, const std::vector<Row>& ram, const ibString& what) {
		ASSERT_EQ(server.size(), ram.size()) << what;
		for (size_t i = 0; i < server.size(); ++i) {
			EXPECT_EQ(server[i].key, ram[i].key) << what << " row " << i;
			for (size_t f = 0; f < server[i].figures.size() && f < ram[i].figures.size(); ++f)
				EXPECT_EQ(0, server[i].figures[f].Compare(ram[i].figures[f]))
					<< what << " " << server[i].key << " figure " << f << ": server " << server[i].figures[f].ToString()
					<< ", RAM " << ram[i].figures[f].ToString();
		}
	};

	for (const ibValue& from : { ibValue(ibDateTime(2026, 3, 1)), ibValue(ibDateTime(2026, 3, 2)) })
	for (const ibRegGranularity kind : { ibRegGranularity::Recorder, ibRegGranularity::Record }) {
		ibRegFold fold;
		fold.m_kind = kind;
		std::vector<ibString> keys = { wxT("Warehouse"), wxT("Item"), reg->GetRegisterPeriod()->GetName() };
		if (fold.HasLineNumber())
			keys.push_back(reg->GetRegisterLineNumber()->GetName());
		const ibString what = (kind == ibRegGranularity::Record ? ibString(wxT("Record")) : ibString(wxT("Recorder")))
			+ wxT(" from ") + from.GetString();

		ibTurnoverQueryable turnovers(reg, from, to, nullptr, fold);
		if (turnovers.IsComputedInRam())
			GTEST_SKIP() << "no totals on this driver - both roads are the RAM one";
		const std::vector<ibString> moved = { wxT("QuantityReceipt"), wxT("QuantityExpense"), wxT("QuantityTurnover") };
		// The RAM road through the reading's own door (ComputeRows) — the register's Compute* are not exported.
		expectSame(served(turnovers, keys, moved), computed(turnovers.ComputeRows({}), keys, moved),
			what + wxT(" turnovers"));

		ibBalanceAndTurnoverQueryable both(reg, from, to, fold);
		const std::vector<ibString> run = { wxT("QuantityOpeningBalance"), wxT("QuantityReceipt"), wxT("QuantityExpense"),
			wxT("QuantityClosingBalance") };
		expectSame(served(both, keys, run), computed(both.ComputeRows({}), keys, run),
			what + wxT(" balance and turnovers"));
	}
}

// ⭐ A LADDER OF TOTALS IS FOLDED BY THE SERVER AS IN MEMORY (2026-11-05). SQLite has no ROLLUP, so two levels read
// every movement into memory to make their headings; one GROUP BY over both levels' fields now returns the items under
// each warehouse, and the warehouses and the grand total add up from them. Ordered by the figure, each level stands in
// the order the rows read by it meet its groups — by the largest movement under each, the largest first: the kitchen
// before the bar, which the alphabet would not put there. The oracle is the movements read plainly and added up here.
TEST_F(AcceptanceAccumulation, ALadderOfTotalsIsFoldedByTheServerAsInMemory)
{
	if (!ready) return;
	PostOpening();
	const ibBackendQueryColumn* const whCol   = warehouse->GetQueryColumn();
	const ibBackendQueryColumn* const itemCol = item->GetQueryColumn();
	const ibBackendQueryColumn* const qtyCol  = reg->GetResourceArrayObject().front()->GetQueryColumn();

	struct Figures { ibNumber sum; ibNumber largest; bool seen = false; };
	std::map<ibString, Figures> byWarehouse;
	std::map<std::pair<ibString, ibString>, Figures> byItem;
	ibNumber total;
	{
		ibDataQueryBuilder rows(ibConnectionPool::ThreadHolder());
		rows.From(reg->GetQueryable());
		rows.Select(whCol, wxT("wh"));
		rows.Select(itemCol, wxT("item"));
		rows.Select(qtyCol, wxT("qty"));
		const auto add = [](Figures& figures, const ibNumber& qty) {
			figures.sum = figures.sum + qty;
			if (!figures.seen || qty.Compare(figures.largest) > 0)
				figures.largest = qty;
			figures.seen = true;
		};
		ibDataQueryResult read = rows.Execute(ibReadPageRequest{});
		while (read.Next()) {
			const ibString wh = read.GetValue(whCol).GetString();
			const ibNumber qty = read.GetValue(qtyCol).GetNumber();
			add(byWarehouse[wh], qty);
			add(byItem[{ wh, read.GetValue(itemCol).GetString() }], qty);
			total = total + qty;
		}
	}
	ASSERT_GT(byItem.size(), byWarehouse.size()) << "a warehouse holds several items";

	ibDataQueryBuilder q(ibConnectionPool::ThreadHolder());
	q.From(reg->GetQueryable());
	q.TotalBy(whCol, ibDimensionKind::Elements);
	q.TotalBy(itemCol, ibDimensionKind::Elements);
	q.Totals().Sum(qtyCol, wxT("Qty"));   // named, as the lowering names it: the server's answer is read back by the name
	q.OrderBy(qtyCol, /*ascending*/ false);
	ibDataQueryResult pushed = ibMakeEmptyQueryResult(nullptr);
	ASSERT_TRUE(q.TryTotalsPushdown(pushed)) << "two levels of sums are folded by the server, ROLLUP or not";

	const ibSelectorTree tree = q.SelectTotals();
	const ibSelectorTree::Node& root = tree.Root();
	EXPECT_EQ(0, root.m_values.at(qtyCol->GetColumnId()).GetNumber().Compare(total)) << "the grand total";
	ASSERT_EQ(root.m_children.size(), byWarehouse.size());
	const ibNumber* whBefore = nullptr;
	for (const std::unique_ptr<ibSelectorTree::Node>& w : root.m_children) {
		const ibString wh = w->m_values.at(whCol->GetColumnId()).GetString();
		const auto at = byWarehouse.find(wh);
		ASSERT_NE(at, byWarehouse.end()) << wh;
		EXPECT_EQ(0, w->m_values.at(qtyCol->GetColumnId()).GetNumber().Compare(at->second.sum)) << wh;
		if (whBefore != nullptr)
			EXPECT_LE(at->second.largest.Compare(*whBefore), 0) << wh << " stands after a warehouse whose largest movement is smaller";
		whBefore = &at->second.largest;
		EXPECT_TRUE(w->m_hasChildren) << wh;

		size_t items = 0;
		const ibNumber* itemBefore = nullptr;
		for (const std::unique_ptr<ibSelectorTree::Node>& i : w->m_children) {
			const ibString code = i->m_values.at(itemCol->GetColumnId()).GetString();
			const auto found = byItem.find({ wh, code });
			ASSERT_NE(found, byItem.end()) << wh << "/" << code;
			EXPECT_EQ(0, i->m_values.at(qtyCol->GetColumnId()).GetNumber().Compare(found->second.sum)) << wh << "/" << code;
			if (itemBefore != nullptr)
				EXPECT_LE(found->second.largest.Compare(*itemBefore), 0) << wh << "/" << code << " stands after a smaller one";
			itemBefore = &found->second.largest;
			++items;
		}
		EXPECT_EQ(items, static_cast<size_t>(std::count_if(byItem.begin(), byItem.end(),
			[&](const auto& kv) { return kv.first.first == wh; }))) << wh;
	}
	EXPECT_EQ(root.m_children.front()->m_values.at(whCol->GetColumnId()).GetString(), ibString(wxT("kitchen")))
		<< "the kitchen moved the most at once";
}

// ⭐ A FIGURE OVER A PROJECTION IS FOLDED BY THE SERVER TOO (2026-11-05). A second fold over one column, a computed
// field: the lowering projects each and folds it through a handle named by the projection. The ladder aggregated that
// name over the bare source, and Firebird answered "Column unknown"; the source is now read with the projection beside
// its row. Twice the quantity, at every heading, is twice the quantity's figure.
TEST_F(AcceptanceAccumulation, AFigureOverAProjectionIsFoldedByTheServer)
{
	if (!ready) return;
	PostOpening();
	const ibBackendQueryColumn* const whCol   = warehouse->GetQueryColumn();
	const ibBackendQueryColumn* const itemCol = item->GetQueryColumn();
	const ibBackendQueryColumn* const qtyCol  = reg->GetResourceArrayObject().front()->GetQueryColumn();
	const ProjectionHandle twice(wxT("Twice"), 1);

	ibDataQueryBuilder q(ibConnectionPool::ThreadHolder());
	q.From(reg->GetQueryable());
	q.SelectExpr(ibQueryColumnExpr::Arith(ibQueryColumnArithOp::Mul, ibQueryColumnExpr::Col(qtyCol),
	                                      ibQueryColumnExpr::Const(ibValue(ibNumber(2)))), wxT("Twice"));
	q.TotalBy(whCol, ibDimensionKind::Elements);
	q.TotalBy(itemCol, ibDimensionKind::Elements);
	q.Totals().Sum(qtyCol, wxT("Qty"));
	q.Totals().Sum(&twice, wxT("TwiceQty"));
	ibDataQueryResult pushed = ibMakeEmptyQueryResult(nullptr);
	ASSERT_TRUE(q.TryTotalsPushdown(pushed)) << "a figure over a projection of the source's row is the server's";

	const ibSelectorTree tree = q.SelectTotals();
	size_t headings = 0;
	const std::function<void(const ibSelectorTree::Node&)> check = [&](const ibSelectorTree::Node& node) {
		const ibNumber qty = node.m_values.at(qtyCol->GetColumnId()).GetNumber();
		EXPECT_EQ(0, node.m_values.at(twice.GetColumnId()).GetNumber().Compare(qty * ibNumber(2))) << "level " << node.m_level;
		++headings;
		for (const std::unique_ptr<ibSelectorTree::Node>& child : node.m_children)
			check(*child);
	};
	check(tree.Root());
	EXPECT_NE(0, tree.Root().m_values.at(qtyCol->GetColumnId()).GetNumber().Compare(ibNumber(0))) << "the movements were read";
	EXPECT_GT(headings, 3u) << "the grand total, the warehouses and their items";
}

TEST_F(AcceptanceAccumulation, RepostingAndUnpostingChangeTheBalance)
{
	if (!ready) return;
	PostOpening();
	ApplyRepostAndUnpost();
}
