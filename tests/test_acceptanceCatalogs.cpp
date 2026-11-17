// =============================================================================
// Accountant's examples — catalogs.
//
// The expected membership and the folder totals come from tools/oracle/acceptance.py
// (tests/fixtures/acceptance/catalogs.json). This file only asks the engine who sits
// under a folder, and adds the oracle's own quantities for those codes.
//
// A deletion mark stays in the tree. The choice a person is offered leaves the marked
// item out, and a contract is offered only for its owner. Both of those are asked
// through the engine. Where the engine still disagrees, the test is a known gap (#234)
// and turns on when the engine matches the oracle.
// =============================================================================

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <wx/filename.h>
#include <wx/init.h>

#include <nlohmann/json.hpp>

#include "backend/appData.h"
#include "backend/backend_composition.h"
#include "backend/composition/dataComposer.h"
#include "backend/compiler/compileCode.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/partial/catalog.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/propertyManager/property/propertyOwner.h"
#include "backend/query/columnLayout.h"
#include "backend/query/provider/database/columnCodec.h"
#include "backend/query/dataQueryBuilder.h"
#include "backend/query/queryHierarchy.h"
#include "backend/query/provider/queryComposer.h"
#include "backend/query/queryRamTable.h"
#include "backend/query/queryableFactory.h"   // ibQueryableSourceDescriptor — the journal, found by its name
#include "backend/query/queryConstructorModel.h"   // ibQueryConstructorModel::GetReferenceBranches — the field tree's [+]
#include "backend/moduleManager/moduleManager.h"   // ibValueModuleManager::ibValueDataUnit — Data.Platform.Journal.Events
#include "backend/system/value/valueQueryable.h"   // ibValueQueryable — what it hands out
#include "backend/objCtor.h"   // ibCtorMetaValueType — a catalog's reference class
#include "core/description/typeDescription.h"
#include "core/exception.h"   // ibCoreException — the journal's refusal of a period that is no date
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

std::set<ibString> AsSet(const nlohmann::json& array)
{
	std::set<ibString> out;
	for (const auto& item : array)
		out.insert(AsWx(item));
	return out;
}

ibString SqliteTypeOf(const ibString& field)
{
	if (field.EndsWith(wxT("_RRRef"))) return wxT(" BLOB");
	if (field.EndsWith(wxT("_D")) || field.EndsWith(wxT("_S"))) return wxT(" TEXT");
	if (field.EndsWith(wxT("_N"))) return wxT(" NUMERIC");
	return wxT(" INTEGER");
}

struct AcceptanceCatalogs : ::testing::Test {
	wxInitializer m_wxInit;
	// RunDatabase sets this thread's code style to the configuration's. The binary runs VES
	// (test_compiler.cpp), so TearDown puts it back, as StyleGuard does.
	const short m_codeStyle = ibCompileCode::GetCodeStyle();
	std::shared_ptr<ibDatabaseLayerSQLite> db;
	ibString dbPath;
	std::unique_ptr<ibMetaDataConfigurationFile> cfg;
	ibValueMetaObjectCatalog* goods = nullptr;
	ibValueMetaObjectCatalog* parties = nullptr;
	ibValueMetaObjectCatalog* contracts = nullptr;
	nlohmann::json fixture;
	std::map<ibString, ibValue> goodsRef;
	std::map<ibString, ibValue> partyRef;
	std::map<ibString, ibValue> contractRef;
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
		// A file, not :memory:. The choice list is sorted by each row's presentation, and that
		// read checks a second connection out of the pool while the scan still holds the first.
		// A clone of :memory: is a different database, so the presentation read would not see
		// the rows this test just wrote.
		dbPath = wxFileName::CreateTempFileName(wxT("oesacc"));
		if (!db->Open(dbPath))
			GTEST_SKIP() << "SQLite open failed";
		pool->Init(db, /*maxSize=*/4, /*minIdle=*/0);

		fixture = LoadFixture(wxT("catalogs.json"));
		cfg = std::make_unique<ibMetaDataConfigurationFile>();
		ibValueMetaObjectConfiguration* root = cfg->GetCommonMetaObject();
		ASSERT_NE(root, nullptr);

		goods = dynamic_cast<ibValueMetaObjectCatalog*>(
			cfg->CreateMetaObject(g_metaCatalogCLSID, root, /*runObject*/ false));
		parties = dynamic_cast<ibValueMetaObjectCatalog*>(
			cfg->CreateMetaObject(g_metaCatalogCLSID, root, false));
		contracts = dynamic_cast<ibValueMetaObjectCatalog*>(
			cfg->CreateMetaObject(g_metaCatalogCLSID, root, false));
		ASSERT_NE(goods, nullptr);
		ASSERT_NE(parties, nullptr);
		ASSERT_NE(contracts, nullptr);
		goods->SetName(wxT("Goods"));
		parties->SetName(wxT("Counterparties"));
		contracts->SetName(wxT("Contracts"));
		// Counterparties and contracts are flat lists. Goods keeps the default: folders and items.
		parties->SetHierarchyType(ibHierarchyType::eNone);
		contracts->SetHierarchyType(ibHierarchyType::eNone);

		// List owner, before the configuration runs. The value itself does not notify: the
		// resolve pass reads it and types the Owner column from the reference constructor,
		// which the register pass has just published. That constructor's class is what a
		// stored reference carries, so a later Owner = comparison can match the row.
		auto* listOwner = dynamic_cast<ibPropertyOwner*>(contracts->GetProperty(ibT("ListOwner")));
		ASSERT_NE(listOwner, nullptr);
		listOwner->SetValue(ibMetaDescription(parties->GetMetaID()));

		ASSERT_TRUE(cfg->RunDatabase()) << "the configuration in memory did not run";
		ASSERT_GT(contracts->GetCatalogOwner()->GetClsidCount(), 0u);

		CreateTable(goods);
		CreateTable(parties);
		CreateTable(contracts);
		InsertGoods(fixture["goods"]["nodes"]);
		InsertParties();
		InsertContracts();
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

	void CreateTable(ibValueMetaObjectCatalog* catalog) {
		ibString list;
		std::set<ibString> seen;
		for (const ibValueMetaObjectAttributeBase* attribute : catalog->GetGenericAttributeArrayObject()) {
			if (attribute == nullptr || attribute->GetQueryColumn() == nullptr)
				continue;
			for (const ibString& field : ColumnFieldNames(*attribute->GetQueryColumn())) {
				if (!seen.insert(field).second)
					continue;
				list << (list.IsEmpty() ? wxT("") : wxT(", ")) << field << SqliteTypeOf(field);
			}
		}
		ASSERT_FALSE(list.IsEmpty());
		db->RunQuery(wxT("%s"), wxT("CREATE TABLE ") + catalog->GetQueryable()->GetQueryTableName()
			+ wxT(" (") + list + wxT(")"));
	}

	void Assign(std::vector<ibDmlAssign>& out, const std::shared_ptr<const ibBackendQueryColumn>& column, const ibValue& value) const {
		const std::vector<ibString> fields = ColumnFieldNames(*column);
		ibQueryStatement capture(ibQueryStatement::Kind::Delete, ibString(), fields);
		int position = 1;
		ibColumnCodec::WriteValue(*column, cfg.get(), value, &capture, position);
		const std::vector<ibQueryExprPtr>& consts = capture.CapturedValues();
		for (size_t i = 0; i < fields.size(); ++i)
			out.push_back(ibDmlAssign{ fields[i], (i < consts.size() && consts[i]) ? consts[i] : ibConst(ibValue()) });
	}

	ibValue NewRef(ibValueMetaObjectCatalog* catalog) const {
		return ibValue(ibValueReferenceDataObject::Create(cfg.get(), catalog->GetMetaID(), ibGuid(ibGuid::newGuid())));
	}

	void InsertRow(ibValueMetaObjectCatalog* catalog, const ibValue& self, const nlohmann::json& node,
		const ibValue& parent, const ibValue& owner) {
		std::vector<ibDmlAssign> row;
		Assign(row, catalog->GetDataReference()->GetQueryColumn(), self);
		Assign(row, catalog->GetDataDeletionMark()->GetQueryColumn(), ibValue(node.value("deletion_mark", false)));
		Assign(row, catalog->GetDataCode()->GetQueryColumn(), ibValue(AsWx(node["code"])));
		Assign(row, catalog->GetDataDescription()->GetQueryColumn(), ibValue(AsWx(node["code"])));
		const std::string predefined = node.value("predefined", std::string());
		Assign(row, catalog->GetDataPredefinedName()->GetQueryColumn(), ibValue(ibString::FromUTF8(predefined)));
		if (catalog->HasParentLink())
			Assign(row, catalog->GetDataParent()->GetQueryColumn(), parent);
		if (catalog->HasFolders())
			Assign(row, catalog->GetDataIsFolder()->GetQueryColumn(), ibValue(node.value("folder", false)));
		if (catalog->GetCatalogOwner()->GetClsidCount() > 0)
			Assign(row, catalog->GetCatalogOwner()->GetQueryColumn(), owner);
		ibDatabaseQueryBuilder insert;
		ASSERT_GE(insert.Execute(ibInsert(catalog->GetQueryable()->GetQueryTableName(), row)), 0);
	}

	void InsertGoods(const nlohmann::json& nodes) {
		for (const auto& node : nodes)
			goodsRef[AsWx(node["code"])] = NewRef(goods);
		for (const auto& node : nodes) {
			const ibString code = AsWx(node["code"]);
			const ibString parent = AsWx(node["parent"]);
			const ibValue parentRef = parent.IsEmpty() ? ibValue() : goodsRef[parent];
			InsertRow(goods, goodsRef[code], node, parentRef, ibValue());
		}
	}

	void InsertParties() {
		for (const auto& node : fixture["contracts"]["owners"]) {
			nlohmann::json row = node;
			row["deletion_mark"] = false;
			row["folder"] = false;
			const ibString code = AsWx(node["code"]);
			partyRef[code] = NewRef(parties);
			InsertRow(parties, partyRef[code], row, ibValue(), ibValue());
		}
	}

	void InsertContracts() {
		for (const auto& node : fixture["contracts"]["items"]) {
			nlohmann::json row = node;
			row["folder"] = false;
			row["predefined"] = "";
			const ibString code = AsWx(node["code"]);
			contractRef[code] = NewRef(contracts);
			InsertRow(contracts, contractRef[code], row, ibValue(), partyRef[AsWx(node["owner"])]);
		}
	}

	void DeleteGoods(const ibString& code) {
		std::vector<ibDmlAssign> key;
		Assign(key, goods->GetDataReference()->GetQueryColumn(), goodsRef[code]);
		ibQueryExprPtr where;
		for (const ibDmlAssign& one : key) {
			const ibQueryExprPtr term = ibBinOp(ibQueryBinOp::Eq, ibCol(one.m_column), one.m_value);
			where = where ? ibBinOp(ibQueryBinOp::And, where, term) : term;
		}
		ibDatabaseQueryBuilder drop;
		ASSERT_GE(drop.Execute(ibDelete(goods->GetQueryable()->GetQueryTableName(), where)), 0);
	}

	ibQueryHierarchyScope Scope(const ibString& root, ibQueryDimUnfold unfold) const {
		return ibQueryHierarchyScope(goods->GetQueryable(), goods->GetDataParent()->GetQueryColumn(),
			{ goodsRef.at(root) }, unfold);
	}

	std::set<ibString> Admitted(const ibQueryHierarchyScope& scope) const {
		std::set<ibString> codes;
		for (const auto& one : goodsRef)
			if (scope.Admits(one.second))
				codes.insert(one.first);
		return codes;
	}

	std::set<ibString> CodesWhere(ibValueMetaObjectCatalog* catalog, const std::shared_ptr<const ibBackendQueryColumn>& extra,
		const ibValue& extraValue, bool unmarkedOnly) {
		ibDataQueryBuilder q(ibConnectionPool::ThreadHolder());
		q.From(catalog->GetQueryable());
		q.Select(catalog->GetDataCode()->GetQueryColumn(), wxT("code"));
		if (unmarkedOnly)
			q.Where(catalog->GetDataDeletionMark()->GetQueryColumn(), ibQueryFilterOp::Equal, ibValue(false));
		if (extra != nullptr)
			q.Where(extra, extraValue);
		ibDataQueryResult rows = q.Execute(ibReadPageRequest{});
		std::set<ibString> codes;
		while (rows.Next())
			codes.insert(rows.GetValue(catalog->GetDataCode()->GetQueryColumn()).GetString());
		return codes;
	}

	std::set<ibString> CodesInHierarchy(const ibString& root) {
		ibQueryCondition cond;
		cond.m_col = goods->GetDataReference()->GetQueryColumn();
		cond.m_op = ibQueryFilterOp::In;
		cond.m_unfold = ibQueryDimUnfold::Hierarchy;
		cond.m_values.push_back(goodsRef.at(root));
		ibDataQueryBuilder q(ibConnectionPool::ThreadHolder());
		q.From(goods->GetQueryable());
		q.Select(goods->GetDataCode()->GetQueryColumn(), wxT("code"));
		q.Where(cond);
		ibDataQueryResult rows = q.Execute(ibReadPageRequest{});
		std::set<ibString> codes;
		while (rows.Next())
			codes.insert(rows.GetValue(goods->GetDataCode()->GetQueryColumn()).GetString());
		return codes;
	}

	ibNumber RolledUnder(const ibString& root) {
		std::shared_ptr<const ibBackendQueryColumn> refCol = goods->GetDataReference()->GetQueryColumn();
		std::shared_ptr<const ibBackendQueryColumn> qtySlot = goods->GetDataCode()->GetQueryColumn();
		ibQueryRamTable snap;
		snap.AddColumn(refCol->GetColumnId(), refCol->GetName(), refCol->GetTypeDesc());
		snap.AddColumn(qtySlot->GetColumnId(), wxT("qty"), ibTypeDescription());
		for (const auto& node : fixture["goods"]["nodes"]) {
			const long row = snap.AppendRow();
			snap.SetCell(row, refCol->GetColumnId(), goodsRef.at(AsWx(node["code"])));
			snap.SetCell(row, qtySlot->GetColumnId(), ibValue(ibNumber(AsWx(node["qty"]))));
		}
		const ibAggregateItem total{ ibAggregateFn::Sum, qtySlot, wxT("qty") };
		const ibSelectorTree tree = ibQueryComposer::BuildReferenceHierarchy(
			snap, refCol, { total }, ibConnectionPool::ThreadHolder(),
			goods->GetQueryable(), ibDimensionKind::Hierarchy);
		const ibSelectorTree::Node* found = FindNode(tree.Root(), refCol->GetColumnId(), goodsRef.at(root));
		if (found == nullptr)
			return ibNumber();
		const ibValue* figure = found->m_values.find_value(qtySlot->GetColumnId());
		return figure != nullptr ? figure->GetNumber() : ibNumber();
	}

	static const ibSelectorTree::Node* FindNode(const ibSelectorTree::Node& node, ibMetaID refId, const ibValue& want) {
		if (const ibValue* key = node.m_values.find_value(refId))
			if (key->CompareValueEQ(want))
				return &node;
		for (const auto& child : node.m_children)
			if (const ibSelectorTree::Node* found = FindNode(*child, refId, want))
				return found;
		return nullptr;
	}

	// THE LIST A PERSON SEES, narrowed by one line of its filter — the composer's own read of the
	// contracts, the road a dynamic list takes, not a hand-built condition.
	std::set<ibString> ContractsWhere(const ibString& path, ibComparisonKind comparison, const ibValue& value) {
		ibDataDBComposer composer;
		composer.FromSource(contracts->GetQueryable());   // as its descriptor hands it out
		composer.Select(contracts->GetDataCode()->GetQueryColumn());
		composer.UserSettings().m_filter.Append(path, comparison, ibStoreValue(value));
		std::vector<ibQueryLowering::ibQueryOutputColumn> schema;
		bool hasTotals = false;
		ibDataQueryResult rows = composer.Execute(schema, hasTotals);
		std::set<ibString> codes;
		while (rows.Next())
			codes.insert(rows.GetValue(contracts->GetDataCode()->GetQueryColumn()).GetString());
		return codes;
	}

	std::set<ibString> ChoiceCodes(ibValueMetaObjectCatalog* catalog) {
		ibValue holder(ibValueReferenceDataObject::Create(cfg.get(), catalog->GetMetaID(), ibGuid(ibGuid::newGuid())));
		ibValueReferenceDataObject* ref = holder.ConvertToType<ibValueReferenceDataObject>();
		std::vector<ibValue> list;
		if (ref != nullptr)
			ref->FindValue(wxEmptyString, ibChoiceCondition(), list);
		std::map<ibString, ibValue> all = goodsRef;
		all.insert(partyRef.begin(), partyRef.end());
		all.insert(contractRef.begin(), contractRef.end());
		std::set<ibString> codes;
		for (const ibValue& one : list)
			for (const auto& known : all)
				if (known.second.CompareValueEQ(one))
					codes.insert(known.first);
		return codes;
	}
};

} // namespace

TEST_F(AcceptanceCatalogs, AFolderContainsItsItemsAndTheFoldersUnderIt)
{
	if (!ready) return;
	const auto& before = fixture["goods"]["before"];
	for (auto it = before["in_hierarchy"].begin(); it != before["in_hierarchy"].end(); ++it) {
		EXPECT_EQ(Admitted(Scope(AsWx(it.key()), ibQueryDimUnfold::Hierarchy)), AsSet(it.value()))
			<< it.key();
	}
	EXPECT_EQ(Admitted(Scope(wxT("FOOD"), ibQueryDimUnfold::HierarchyOnly)),
		AsSet(before["hierarchy_only"]["FOOD"]));
	for (auto it = before["totals"].begin(); it != before["totals"].end(); ++it) {
		const ibNumber got = RolledUnder(AsWx(it.key()));
		EXPECT_EQ(0, got.Compare(ibNumber(AsWx(it.value())))) << it.key() << " " << got.ToString();
	}
	for (auto it = before["in_hierarchy"].begin(); it != before["in_hierarchy"].end(); ++it) {
		EXPECT_EQ(CodesInHierarchy(AsWx(it.key())), AsSet(it.value())) << it.key();
	}
}

TEST_F(AcceptanceCatalogs, MovingAnItemChangesTheFolderItSumsUnder)
{
	if (!ready) return;
	const ibString code = AsWx(fixture["goods"]["move"]["code"]);
	const ibString to = AsWx(fixture["goods"]["move"]["to"]);
	nlohmann::json node;
	for (const auto& one : fixture["goods"]["nodes"])
		if (AsWx(one["code"]) == code)
			node = one;
	ASSERT_FALSE(node.is_null());
	DeleteGoods(code);
	InsertRow(goods, goodsRef[code], node, goodsRef[to], ibValue());

	const auto& after = fixture["goods"]["after"];
	EXPECT_EQ(Admitted(Scope(wxT("FOOD"), ibQueryDimUnfold::Hierarchy)), AsSet(after["in_hierarchy"]["FOOD"]));
	EXPECT_EQ(Admitted(Scope(wxT("TOOLS"), ibQueryDimUnfold::Hierarchy)), AsSet(after["in_hierarchy"]["TOOLS"]));
	EXPECT_EQ(0, RolledUnder(wxT("FOOD")).Compare(ibNumber(AsWx(after["totals"]["FOOD"]))));
	EXPECT_EQ(0, RolledUnder(wxT("TOOLS")).Compare(ibNumber(AsWx(after["totals"]["TOOLS"]))));
	EXPECT_EQ(0, RolledUnder(wxT("GOODS")).Compare(ibNumber(AsWx(after["totals"]["GOODS"]))));
}

// Disabled, not skipped on a mismatch: known gap #234 — the choice still offers an item
// marked for deletion. Enabled by the change that closes it.
TEST_F(AcceptanceCatalogs, DISABLED_ADeletionMarkStaysInTheTreeAndTheChoiceStillShowsIt)
{
	if (!ready) return;
	const ibString marked = AsWx(fixture["choice"]["marked_code"]);
	EXPECT_TRUE(Admitted(Scope(wxT("FOOD"), ibQueryDimUnfold::Hierarchy)).count(marked))
		<< "a mark does not take the item out of the tree";

	EXPECT_EQ(CodesWhere(goods, nullptr, ibValue(), true), AsSet(fixture["choice"]["offered"]));

	// The accountant's list leaves the marked item out. FindValue still offers it (#234).
	EXPECT_EQ(ChoiceCodes(goods), AsSet(fixture["choice"]["offered"]));
}

TEST_F(AcceptanceCatalogs, AQueryNarrowsASubordinateCatalogToItsOwner)
{
	if (!ready) return;
	const std::set<ibString> partiesSeen = CodesWhere(parties, nullptr, ibValue(), false);
	EXPECT_TRUE(partiesSeen.count(wxT("ACME")));
	EXPECT_TRUE(partiesSeen.count(wxT("GLOBEX")));

	const auto& block = fixture["contracts"];
	std::shared_ptr<const ibBackendQueryColumn> ownerCol = contracts->GetCatalogOwner()->GetQueryColumn();
	// Where(owner, ref) narrows on this tree. A tree that stops narrowing fails here, and that is
	// the regression to fix, not a reason to edit the oracle.
	for (auto it = block["of_owner_including_marked"].begin(); it != block["of_owner_including_marked"].end(); ++it)
		EXPECT_EQ(CodesWhere(contracts, ownerCol, partyRef[AsWx(it.key())], false), AsSet(it.value()));
}

// «CONTAINS» IS A SUBSTRING, on a stored list as in memory. The condition used to reach the
// database as `LIKE 'value'` — an exact match — so "contains 1" found nothing while "%1%" did.
TEST_F(AcceptanceCatalogs, AContainsConditionOnAStoredListMatchesASubstring)
{
	if (!ready) return;
	EXPECT_EQ(ContractsWhere(wxT("Code"), ibComparisonKind_Contains, ibValue(ibT("3"))),
		(std::set<ibString>{ wxT("C3") }));
	EXPECT_EQ(ContractsWhere(wxT("Code"), ibComparisonKind_Contains, ibValue(ibT("C"))),
		(std::set<ibString>{ wxT("C1"), wxT("C2"), wxT("C3") }));
	// …through a reference, too: the owner's code, read by a join.
	EXPECT_EQ(ContractsWhere(wxT("Owner.Code"), ibComparisonKind_Contains, ibValue(ibT("CM"))),
		(std::set<ibString>{ wxT("C1"), wxT("C2") }));
	// A wildcard the person typed stays a wildcard, as it does in memory (ibCompositionCompare).
	EXPECT_EQ(ContractsWhere(wxT("Owner.Code"), ibComparisonKind_Contains, ibValue(ibT("G_OBEX"))),
		(std::set<ibString>{ wxT("C3") }));

	// …while a LIKE condition of the query builder (ibQueryFilterOp::Like) keeps the pattern its author wrote: the
	// wrapping is the filter's «contains», not the LIKE's.
	ibQueryCondition like;
	like.m_col = contracts->GetDataCode()->GetQueryColumn();
	like.m_op = ibQueryFilterOp::Like;
	like.m_value = ibValue(ibT("C"));
	ibDataQueryBuilder q(ibConnectionPool::ThreadHolder());
	q.From(contracts->GetQueryable());
	q.Select(contracts->GetDataCode()->GetQueryColumn(), wxT("code"));
	q.Where(like);
	ibDataQueryResult rows = q.Execute(ibReadPageRequest{});
	EXPECT_FALSE(rows.Next()) << "LIKE 'C' is not a substring match";
}

// ⭐ EVERY CONFIGURATION'S FACTORY FINDS THE REGISTRATION JOURNAL — by the name a query writes, with the period the
// turnovers take, and read by the computed provider: in memory, never in a statement of the base.
TEST_F(AcceptanceCatalogs, EveryConfigurationsFactoryFindsTheJournal)
{
	if (!ready) return;
	ibQueryableFactory* const factory = cfg->GetSourceFactory();
	ASSERT_NE(factory, nullptr);
	ibQueryableSourceDescriptor* const journal = factory->FindDescriptor(wxT("Journal"), wxT("Events"));
	ASSERT_NE(journal, nullptr);
	EXPECT_NE(journal->GetPicture(), 0u) << "the constructor's list draws it with a picture of its own";

	std::vector<ibQuerySourceParameter> parameters;
	journal->DescribeParameters(parameters);
	ASSERT_EQ(parameters.size(), 2u);
	EXPECT_EQ(parameters[0].m_name, ibString(wxT("BeginOfPeriod")));
	EXPECT_EQ(parameters[1].m_name, ibString(wxT("EndOfPeriod")));

	const std::shared_ptr<const ibBackendQueryable> events = journal->CreateQueryable(nullptr, 0, {}, ibQueryReadColumns());
	ASSERT_NE(events, nullptr);
	std::vector<ibString> names;
	for (const std::shared_ptr<const ibBackendQueryColumn>& column : events->GetQueryColumns())
		names.push_back(column->GetName());
	EXPECT_EQ(names, (std::vector<ibString>{ wxT("Date"), wxT("Level"), wxT("Session"), wxT("User"), wxT("Host"),
		wxT("Source"), wxT("Event"), wxT("Message"), wxT("Object"), wxT("Details") }));
	EXPECT_TRUE(events->GetProvider().IsComputedInRam());
	EXPECT_FALSE(events->GetProvider().CanJoinOn(nullptr)) << "the journal's rows never go into a statement of the base";

	// The period is a pair of moments; anything else is refused, by the source's name.
	ibValue word(ibT("yesterday"));
	ibValue* args[] = { &word };
	EXPECT_THROW(journal->CreateQueryable(args, 1, {}, ibQueryReadColumns()), ibCoreException);
}

// ⭐ …AND IT IS THE PLATFORM'S, NOT THE CONFIGURATION'S — the factory names it among its platform sources (a catalog it
// does not), the constructor's catalogue draws it under Platform, and LINQ reads it as Data.Platform.Journal.Events:
// the same source, by the namespace and the name a query writes.
TEST_F(AcceptanceCatalogs, TheJournalIsThePlatformsSource)
{
	if (!ready) return;
	ibQueryableFactory* const factory = cfg->GetSourceFactory();
	ASSERT_NE(factory, nullptr);
	ibQueryableSourceDescriptor* const journal = factory->FindDescriptor(wxT("Journal"), wxT("Events"));
	ASSERT_NE(journal, nullptr);
	ibQueryableSourceDescriptor* const contractsSource = factory->FindDescriptor(wxT("Catalog"), wxT("Contracts"));
	ASSERT_NE(contractsSource, nullptr);
	const std::vector<ibQueryableSourceDescriptor*> platform = factory->GetDescriptors(ibQueryableSourceKind::Platform);
	EXPECT_NE(std::find(platform.begin(), platform.end(), journal), platform.end());
	EXPECT_EQ(std::find(platform.begin(), platform.end(), contractsSource), platform.end());

	int journalRows = 0, contractRows = 0;
	for (const ibQueryConstructorSource& source : ibQueryConstructorModel(cfg.get()).GetSources()) {
		if (source.Text() == ibString(wxT("Journal.Events"))) {
			++journalRows;
			EXPECT_TRUE(source.m_platform) << "drawn under Platform";
		}
		if (source.Text() == ibString(wxT("Catalog.Contracts"))) {
			++contractRows;
			EXPECT_FALSE(source.m_platform) << "a catalog is the configuration's";
		}
	}
	EXPECT_EQ(journalRows, 1);
	EXPECT_EQ(contractRows, 1);

	auto prop = [](ibValue& value, const wxChar* name) {
		ibValue result;
		const long number = value.FindProp(name);
		EXPECT_NE(number, wxNOT_FOUND) << name;
		if (number != wxNOT_FOUND)
			value.GetPropVal(number, result);
		return result;
	};
	ibValue data = new ibValueModuleManager::ibValueDataUnit(cfg.get());
	ibValue namespaces = prop(data, wxT("Platform"));
	ibValue sources = prop(namespaces, wxT("Journal"));
	ibValue events = prop(sources, wxT("Events"));
	ibValueQueryable* queryable = nullptr;
	ASSERT_TRUE(events.ConvertToValue(queryable) && queryable != nullptr) << "Data.Platform.Journal.Events is a source";
	EXPECT_EQ(queryable->GetSourceName(), ibString(wxT("Journal.Events")));
	ASSERT_NE(queryable->GetQueryable(), nullptr);
	EXPECT_TRUE(queryable->GetQueryable()->GetProvider().IsComputedInRam()) << "read by the computed provider";
}

// ⭐ ITS OBJECT IS DECLARED ANY REFERENCE AND HOLDS THIS CONFIGURATION'S — as a field declared `AnyRef` does
// (ibOpenBarriers), so a field tree opens it by type. It showed a [+] with nothing behind it (2026-11-17): the column
// held the bare barrier, and the constructor's branches are asked per class.
TEST_F(AcceptanceCatalogs, TheJournalsObjectHoldsTheConfigurationsReferences)
{
	if (!ready) return;
	ibQueryableFactory* const factory = cfg->GetSourceFactory();
	ASSERT_NE(factory, nullptr);
	ibQueryableSourceDescriptor* const journal = factory->FindDescriptor(wxT("Journal"), wxT("Events"));
	ASSERT_NE(journal, nullptr);
	const std::shared_ptr<const ibBackendQueryable> events = journal->CreateQueryable(nullptr, 0, {}, ibQueryReadColumns());
	ASSERT_NE(events, nullptr);
	const std::shared_ptr<const ibBackendQueryColumn> object = events->ResolveColumnByName(wxT("Object"));
	ASSERT_NE(object, nullptr);

	ASSERT_EQ(object->GetTypeDesc().GetClsidCount(), 1u);
	EXPECT_TRUE(clsid_is_any(object->GetTypeDesc().GetClsidList().front())) << "declared any reference - what the Fields tab shows";

	const ibCtorMetaValueType* const contractRefCtor = cfg->GetTypeCtor(contracts, ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
	ASSERT_NE(contractRefCtor, nullptr);
	const std::vector<ibClassID>& holds = object->GetTypeValueDesc().GetClsidList();
	EXPECT_NE(std::find(holds.begin(), holds.end(), contractRefCtor->GetClassType()), holds.end())
		<< "a contract is one of the objects an event can be about";

	// …and the tables a settings tree's [+] is answered by (ibWriteSettingsReferences): the reference's own, and the
	// barrier's members. A loop over the opened type's list read a temporary that was gone and answered nothing — every
	// [+] of every field tree vanished (2026-11-17).
	const std::vector<ibMetaID> own = ibValueReferenceDataObject::ConvertToMetaIds({ contractRefCtor->GetClassType() }, cfg.get());
	EXPECT_EQ(own, std::vector<ibMetaID>{ contracts->GetMetaID() });
	const std::vector<ibMetaID> admitted = ibValueReferenceDataObject::ConvertToMetaIds(object->GetTypeDesc().GetClsidList(), cfg.get());
	EXPECT_NE(std::find(admitted.begin(), admitted.end(), contracts->GetMetaID()), admitted.end());

	const ibQueryConstructorModel model(cfg.get());
	bool contractsBranch = false;
	for (const ibQueryConstructorField& branch : model.GetReferenceBranches(object->GetTypeValueDesc()))
		contractsBranch = contractsBranch || branch.m_referenceClsid == contractRefCtor->GetClassType();
	EXPECT_TRUE(contractsBranch) << "the field tree's [+] has the contracts behind it";
}

// ⭐ …AND A QUERY JOINS IT TO A TABLE OF THE BASE, by the name and in the words of any other source. Each side is read by
// its own provider and the two are stitched in memory; no event of the journal is about a contract here, so every
// contract comes once, with no event.
TEST_F(AcceptanceCatalogs, AQueryJoinsTheJournalToACatalog)
{
	if (!ready) return;
	ibDataDBComposer composer;
	composer.SetMetaData(cfg.get());   // the configuration its names resolve in — the journal is this one's
	composer.FromText(wxT("SELECT C.Code AS Code, J.Event AS Event FROM Catalog.Contracts AS C ")
		wxT("LEFT JOIN Journal.Events AS J ON J.Object = C.Ref"));
	std::vector<ibQueryLowering::ibQueryOutputColumn> schema;
	bool hasTotals = false;
	ibDataQueryResult rows = composer.Execute(schema, hasTotals);
	std::vector<ibString> codes;
	while (rows.Next()) {
		codes.push_back(rows.GetColumn(wxT("Code")).GetString());
		EXPECT_TRUE(rows.GetColumn(wxT("Event")).GetString().IsEmpty());
	}
	std::sort(codes.begin(), codes.end());
	std::vector<ibString> expected;
	for (const auto& contract : contractRef)
		expected.push_back(contract.first);   // a map: in order already
	EXPECT_EQ(codes, expected);
}
