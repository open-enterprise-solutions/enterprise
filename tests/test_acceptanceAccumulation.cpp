// =============================================================================
// Accountant's examples — an accumulation register of balances.
//
// Expected figures: tools/oracle/acceptance.py (tests/fixtures/acceptance/accumulation.json).
// The register is the platform's own: ContributeTables declares the totals, the
// triggers keep them, and the balance is read through RenderMaterializedRead.
// A balance at midnight, excluding that midnight, leaves out the day that starts there.
//
// An expense larger than the stock is stored. The fixture says the accountant would
// have refused it; no such gate exists on this write path, so the test expects the
// arithmetic and keeps the question in the JSON.
// =============================================================================

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <wx/filename.h>
#include <wx/init.h>

#include <nlohmann/json.hpp>

#include "backend/appData.h"
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
#include "backend/query/schemaSnapshot.h"
#include "core/clsid.h"

namespace {

nlohmann::json LoadFixture(const wxString& fileName)
{
	wxFileName here(wxString::FromUTF8(__FILE__));
	here.SetFullName(wxEmptyString);
	here.AppendDir(wxT("fixtures"));
	here.AppendDir(wxT("acceptance"));
	here.SetFullName(fileName);
	std::ifstream in(std::string(here.GetFullPath().utf8_str()));
	EXPECT_TRUE(in.good()) << here.GetFullPath();
	return nlohmann::json::parse(in);
}

wxString AsWx(const nlohmann::json& node)
{
	return wxString::FromUTF8(node.get<std::string>());
}

ibDateTime At(const nlohmann::json& node)
{
	int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
	const std::string text = node.get<std::string>();
	if (std::sscanf(text.c_str(), "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6)
		return ibDateTime();
	return ibDateTime(year, static_cast<unsigned>(month), static_cast<unsigned>(day),
		static_cast<unsigned>(hour), static_cast<unsigned>(minute), static_cast<unsigned>(second));
}

wxString SqliteTypeOf(const ibColumnType& type)
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
	wxString doc;
	int line = 0;
	ibDateTime when;
	wxString wh;
	wxString item;
	bool receipt = false;
	wxString qty;
	bool active = true;
};

struct AcceptanceAccumulation : ::testing::Test {
	wxInitializer m_wxInit;
	std::shared_ptr<ibDatabaseLayerSQLite> db;
	wxString dbPath;
	std::unique_ptr<ibMetaDataConfigurationFile> cfg;
	ibValueMetaObject* document = nullptr;
	ibValueMetaObjectAccumulationRegister* reg = nullptr;
	ibValueMetaObjectDimension* warehouse = nullptr;
	ibValueMetaObjectDimension* item = nullptr;
	ibMaterializeSpec readSpec;
	ibMaterializeView turnovers;
	wxString movementsTable;
	wxString receiptAlias;
	wxString expenseAlias;
	wxString turnoverAlias;
	std::vector<wxString> keyColumns;
	std::vector<wxString> warehouseFields;
	std::vector<wxString> itemFields;
	nlohmann::json fixture;
	std::map<wxString, ibValue> recorders;
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

		const std::vector<wxString> periodFields = ColumnFieldNames(reg->GetRegisterPeriod()->GetQueryColumn());
		for (const wxString& field : readSpec.m_keyColumns)
			if (std::find(periodFields.begin(), periodFields.end(), field) == periodFields.end())
				keyColumns.push_back(field);
		const auto keepKeyed = [&](const std::vector<wxString>& fields) {
			std::vector<wxString> kept;
			for (const wxString& field : fields)
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
		wxString list;
		std::set<wxString> seen;
		auto addNamed = [&](const wxString& field, const wxString& sqlType) {
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
				addNamed(wxString(ShardColumnName()), wxT(" INTEGER"));
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
		wxString keys;
		auto addKey = [&](const wxString& field) {
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
			for (const wxString& field : render.m_keyColumns)
				addKey(field);
			// The declaration may ask for several shards. SQLite keeps one: the trigger's
			// conflict target leaves the shard column out, and the index has to name the same columns.
			if (!ibDatabaseLayerSQLite::MaterializationDialect().m_connectionIdExpr.IsEmpty()
				&& render.m_shards > 1)
				addKey(wxString(ShardColumnName()));
		}
		if (!keys.IsEmpty())
			db->RunQuery(wxT("%s"), wxT("CREATE UNIQUE INDEX ") + table.m_name
				+ wxT("_PK ON ") + table.m_name + wxT(" (") + keys + wxT(")"));
	}

	void Assign(std::vector<ibDmlAssign>& out, const ibBackendQueryColumn* column, const ibValue& value) const {
		const std::vector<wxString> fields = ColumnFieldNames(column);
		ibQueryStatement capture(ibQueryStatement::Kind::Delete, wxString(), fields);
		int position = 1;
		ibColumnCodec::WriteValue(column, cfg.get(), value, &capture, position);
		const std::vector<ibQueryExprPtr>& consts = capture.CapturedValues();
		for (size_t i = 0; i < fields.size(); ++i)
			out.push_back(ibDmlAssign{ fields[i], (i < consts.size() && consts[i]) ? consts[i] : ibConst(ibValue()) });
	}

	const ibValue& Recorder(const wxString& doc) {
		const auto found = recorders.find(doc);
		if (found != recorders.end())
			return found->second;
		recorders[doc] = ibValue(ibValueReferenceDataObject::Create(
			cfg.get(), document->GetMetaID(), ibGuid(ibGuid::newGuid())));
		return recorders[doc];
	}

	void Insert(const Movement& movement) {
		const ibValue kind = ibValue::CreateEnumObject<ibValueEnumAccumulationRegisterRecordType>(
			movement.receipt ? ibRecordType::eReceipt : ibRecordType::eExpense);
		std::vector<ibDmlAssign> row;
		Assign(row, reg->GetRegisterActive()->GetQueryColumn(), ibValue(movement.active));
		Assign(row, reg->GetRegisterPeriod()->GetQueryColumn(), ibValue(movement.when));
		Assign(row, reg->GetRegisterRecorder()->GetQueryColumn(), Recorder(movement.doc));
		Assign(row, reg->GetRegisterLineNumber()->GetQueryColumn(), ibValue(ibNumber(movement.line)));
		Assign(row, reg->GetRegisterRecordType()->GetQueryColumn(), kind);
		Assign(row, warehouse->GetQueryColumn(), ibValue(movement.wh));
		Assign(row, item->GetQueryColumn(), ibValue(movement.item));
		Assign(row, reg->GetResourceArrayObject().front()->GetQueryColumn(), ibValue(ibNumber(movement.qty)));
		ibDatabaseQueryBuilder insert;
		ASSERT_GE(insert.Execute(ibInsert(movementsTable, row)), 0) << movement.doc << " " << movement.line;
	}

	void DeleteLine(const wxString& doc, int line) {
		std::vector<ibDmlAssign> key;
		Assign(key, reg->GetRegisterRecorder()->GetQueryColumn(), Recorder(doc));
		Assign(key, reg->GetRegisterLineNumber()->GetQueryColumn(), ibValue(ibNumber(line)));
		ibQueryExprPtr where;
		for (const ibDmlAssign& one : key) {
			const ibQueryExprPtr term = ibBinOp(ibQueryBinOp::Eq, ibCol(one.m_column), one.m_value);
			where = where ? ibBinOp(ibQueryBinOp::And, where, term) : term;
		}
		ibDatabaseQueryBuilder drop;
		ASSERT_GE(drop.Execute(ibDelete(movementsTable, where)), 0);
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

	bool RowMatches(ibQueryResult& row, const std::vector<wxString>& fields, const wxString& want) const {
		for (const wxString& field : fields)
			if (row.GetResultString(field) == want)
				return true;
		return false;
	}

	// The figure for one key. A missing row is zero: a balance of nothing is no row.
	ibNumber Figure(const ibMaterializeReadSpec& spec, const wxString& wh, const wxString& itemCode, const wxString& alias) {
		ibDatabaseQueryBuilder q(ibConnectionPool::ThreadHolder());
		std::vector<ibQueryProjItem> projected;
		projected.push_back(ibQueryProjItem{ ibCol(wxT("b"), alias), alias });
		for (const wxString& field : warehouseFields)
			projected.push_back(ibQueryProjItem{ ibCol(wxT("b"), field), field });
		for (const wxString& field : itemFields)
			projected.push_back(ibQueryProjItem{ ibCol(wxT("b"), field), field });
		q.From(RenderMaterializedRead(spec, wxT("b")));
		q.Project(projected);
		ibQueryResult rows = q.Execute();
		while (rows.Next()) {
			if (RowMatches(rows, warehouseFields, wh) && RowMatches(rows, itemFields, itemCode))
				return rows.GetResultNumber(alias);
		}
		return ibNumber();
	}

	// The cut is the register's own (ibRegFillArmCut). A midnight that is excluded is not given a
	// floor by hand: that grain is wholly out, and a floor at the midnight would take the day in.
	ibMaterializeReadSpec RowsUpTo(const ibDateTime& moment, bool excluding) const {
		ibMaterializeReadSpec spec;
		spec.m_storedRows = RenderStoredRows(readSpec, turnovers);
		spec.m_movedRows = RenderMovementRows(readSpec, turnovers);
		spec.m_rowsAlias = wxT("b_rows");
		spec.m_periodColumn = readSpec.m_periodColumn;
		spec.m_keyColumns = keyColumns;
		spec.m_to = ibValue(moment);
		ibRegBound upper;
		upper.m_date = ibValue(moment);
		upper.m_excluding = excluding;
		ibRegFillArmCut(spec, reg, upper);
		return spec;
	}

	ibMaterializeReadSpec BalanceSpec(const ibDateTime& moment, bool excluding) const {
		ibMaterializeReadSpec spec = RowsUpTo(moment, excluding);
		spec.m_dropZeroRows = true;
		spec.m_columns = {
			{ wxT("bal_"), turnoverAlias, wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::UpToTo, true },
		};
		return spec;
	}

	// [from, to). The end is excluded, so a turnover that stops at midnight leaves the next day out.
	ibMaterializeReadSpec TurnoverSpec(const ibDateTime& from, const ibDateTime& to) const {
		ibMaterializeReadSpec spec = RowsUpTo(to, true);
		spec.m_from = ibValue(from);
		spec.m_dropZeroRows = true;
		spec.m_columns = {
			{ wxT("in_"), receiptAlias, wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::InRange, true },
			{ wxT("out_"), expenseAlias, wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::InRange, true },
		};
		ibRegBound lower;
		lower.m_date = ibValue(from);
		ibRegBound upper;
		upper.m_date = ibValue(to);
		upper.m_excluding = true;
		ibRegFillArmCut(spec, reg, upper, lower);
		return spec;
	}

	void ExpectBalance(const nlohmann::json& question) {
		ASSERT_EQ(question["kind"], "balance");
		const ibNumber got = Figure(BalanceSpec(At(question["at"]), question["excluding"].get<bool>()),
			AsWx(question["wh"]), AsWx(question["item"]), wxT("bal_"));
		const ibNumber want(AsWx(question["qty"]));
		EXPECT_EQ(0, got.Compare(want)) << question["wh"] << " " << question["item"]
			<< " at " << question["at"] << " excluding=" << question["excluding"]
			<< " got " << got.ToString() << " want " << want.ToString();
	}
};

} // namespace

TEST_F(AcceptanceAccumulation, TheBalanceAtMidnightLeavesOutTheDayThatStarts)
{
	if (!ready) return;
	PostOpening();
	for (const auto& question : fixture["balances"])
		ExpectBalance(question);
}

TEST_F(AcceptanceAccumulation, TurnoversRunFromMidnightUpToTheNext)
{
	if (!ready) return;
	PostOpening();
	for (const auto& question : fixture["turnovers"]) {
		const ibMaterializeReadSpec spec = TurnoverSpec(At(question["from"]), At(question["to"]));
		const wxString wh = AsWx(question["wh"]);
		const wxString itemCode = AsWx(question["item"]);
		const ibNumber receipt = Figure(spec, wh, itemCode, wxT("in_"));
		const ibNumber expense = Figure(spec, wh, itemCode, wxT("out_"));
		EXPECT_EQ(0, receipt.Compare(ibNumber(AsWx(question["receipt"])))) << wh << " " << itemCode << " " << receipt.ToString();
		EXPECT_EQ(0, expense.Compare(ibNumber(AsWx(question["expense"])))) << wh << " " << itemCode << " " << expense.ToString();
	}
}

TEST_F(AcceptanceAccumulation, BalanceAndTurnoversIsTheOpeningPlusTheMonth)
{
	if (!ready) return;
	PostOpening();
	for (const auto& question : fixture["balance_and_turnovers"]) {
		const wxString wh = AsWx(question["wh"]);
		const wxString itemCode = AsWx(question["item"]);
		const ibDateTime from = At(question["from"]);
		const ibDateTime to = At(question["to"]);
		const ibNumber opening = Figure(BalanceSpec(from, true), wh, itemCode, wxT("bal_"));
		const ibMaterializeReadSpec moved = TurnoverSpec(from, to);
		const ibNumber receipt = Figure(moved, wh, itemCode, wxT("in_"));
		const ibNumber expense = Figure(moved, wh, itemCode, wxT("out_"));
		const ibNumber closing = Figure(BalanceSpec(to, true), wh, itemCode, wxT("bal_"));
		EXPECT_EQ(0, opening.Compare(ibNumber(AsWx(question["opening"])))) << opening.ToString();
		EXPECT_EQ(0, receipt.Compare(ibNumber(AsWx(question["receipt"])))) << receipt.ToString();
		EXPECT_EQ(0, expense.Compare(ibNumber(AsWx(question["expense"])))) << expense.ToString();
		EXPECT_EQ(0, closing.Compare(ibNumber(AsWx(question["closing"])))) << closing.ToString();
		EXPECT_EQ(0, closing.Compare(opening + receipt - expense));
	}
}

TEST_F(AcceptanceAccumulation, RepostingAndUnpostingChangeTheBalance)
{
	if (!ready) return;
	PostOpening();
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
	Movement silent;
	for (const Movement& movement : posted)
		if (movement.doc == AsWx(unpost["doc"]) && movement.line == unpost["line"].get<int>())
			silent = movement;
	silent.active = false;
	Insert(silent);
	ExpectBalance(unpost["then"]);
}

TEST_F(AcceptanceAccumulation, AnExpensePastTheStockIsStored)
{
	if (!ready) return;
	PostOpening();
	const auto& repost = fixture["repost"];
	DeleteLine(AsWx(repost["doc"]), repost["line"].get<int>());
	Movement again;
	for (const Movement& movement : posted)
		if (movement.doc == AsWx(repost["doc"]) && movement.line == repost["line"].get<int>())
			again = movement;
	again.qty = AsWx(repost["qty"]);
	Insert(again);
	const auto& unpost = fixture["unpost"];
	DeleteLine(AsWx(unpost["doc"]), unpost["line"].get<int>());
	Movement silent;
	for (const Movement& movement : posted)
		if (movement.doc == AsWx(unpost["doc"]) && movement.line == unpost["line"].get<int>())
			silent = movement;
	silent.active = false;
	Insert(silent);

	const auto& over = fixture["negative"];
	ASSERT_EQ(over["platform"], "accepted");
	ASSERT_EQ(over["accountant_expects"], "refused");
	Movement expense;
	expense.doc = AsWx(over["doc"]);
	expense.line = over["line"].get<int>();
	expense.when = At(over["when"]);
	expense.wh = AsWx(over["wh"]);
	expense.item = AsWx(over["item"]);
	expense.receipt = over["receipt"].get<bool>();
	expense.qty = AsWx(over["qty"]);
	expense.active = true;
	Insert(expense);
	ExpectBalance(over["then"]);
}
