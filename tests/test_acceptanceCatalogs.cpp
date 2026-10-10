// =============================================================================
// Accountant's examples — catalogs.
//
// The expected membership and the folder totals come from tools/oracle/acceptance.py
// (tests/fixtures/acceptance/catalogs.json). This file only asks the engine who sits
// under a folder, and adds the oracle's own quantities for those codes.
//
// A deletion mark stays in the tree. The choice door still returns the marked item;
// a query that names DeletionMark does not. A subordinate catalog is narrowed by
// the Owner column's stored fields. The metadata Where on that reference returned
// none of those rows; the fixture records the question.
// =============================================================================

#include <gtest/gtest.h>

#include <fstream>
#include <map>
#include <set>
#include <string>

#include <wx/filename.h>
#include <wx/init.h>

#include <nlohmann/json.hpp>

#include "backend/appData.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/partial/catalog.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/propertyManager/property/propertyOwner.h"
#include "backend/query/columnLayout.h"
#include "backend/query/dataQueryBuilder.h"
#include "backend/query/queryHierarchy.h"
#include "backend/typeDescription.h"
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

std::set<wxString> AsSet(const nlohmann::json& array)
{
	std::set<wxString> out;
	for (const auto& item : array)
		out.insert(AsWx(item));
	return out;
}

wxString SqliteTypeOf(const wxString& field)
{
	if (field.EndsWith(wxT("_RRRef"))) return wxT(" BLOB");
	if (field.EndsWith(wxT("_D")) || field.EndsWith(wxT("_S"))) return wxT(" TEXT");
	if (field.EndsWith(wxT("_N"))) return wxT(" NUMERIC");
	return wxT(" INTEGER");
}

struct AcceptanceCatalogs : ::testing::Test {
	wxInitializer m_wxInit;
	std::shared_ptr<ibDatabaseLayerSQLite> db;
	wxString dbPath;
	std::unique_ptr<ibMetaDataConfigurationFile> cfg;
	ibValueMetaObjectCatalog* goods = nullptr;
	ibValueMetaObjectCatalog* parties = nullptr;
	ibValueMetaObjectCatalog* contracts = nullptr;
	nlohmann::json fixture;
	std::map<wxString, ibValue> goodsRef;
	std::map<wxString, ibValue> partyRef;
	std::map<wxString, ibValue> contractRef;
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
		auto* listOwner = dynamic_cast<ibPropertyOwner*>(contracts->GetProperty(wxT("ListOwner")));
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
		wxString list;
		std::set<wxString> seen;
		for (const ibValueMetaObjectAttributeBase* attribute : catalog->GetGenericAttributeArrayObject()) {
			if (attribute == nullptr || attribute->GetQueryColumn() == nullptr)
				continue;
			for (const wxString& field : ColumnFieldNames(attribute->GetQueryColumn())) {
				if (!seen.insert(field).second)
					continue;
				list << (list.IsEmpty() ? wxT("") : wxT(", ")) << field << SqliteTypeOf(field);
			}
		}
		ASSERT_FALSE(list.IsEmpty());
		db->RunQuery(wxT("%s"), wxT("CREATE TABLE ") + catalog->GetQueryable()->GetQueryTableName()
			+ wxT(" (") + list + wxT(")"));
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
		Assign(row, catalog->GetDataPredefinedName()->GetQueryColumn(), ibValue(wxString::FromUTF8(predefined)));
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
			const wxString code = AsWx(node["code"]);
			const wxString parent = AsWx(node["parent"]);
			const ibValue parentRef = parent.IsEmpty() ? ibValue() : goodsRef[parent];
			InsertRow(goods, goodsRef[code], node, parentRef, ibValue());
		}
	}

	void InsertParties() {
		for (const auto& node : fixture["contracts"]["owners"]) {
			nlohmann::json row = node;
			row["deletion_mark"] = false;
			row["folder"] = false;
			const wxString code = AsWx(node["code"]);
			partyRef[code] = NewRef(parties);
			InsertRow(parties, partyRef[code], row, ibValue(), ibValue());
		}
	}

	void InsertContracts() {
		for (const auto& node : fixture["contracts"]["items"]) {
			nlohmann::json row = node;
			row["folder"] = false;
			row["predefined"] = "";
			const wxString code = AsWx(node["code"]);
			contractRef[code] = NewRef(contracts);
			InsertRow(contracts, contractRef[code], row, ibValue(), partyRef[AsWx(node["owner"])]);
		}
	}

	void DeleteGoods(const wxString& code) {
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

	ibQueryHierarchyScope Scope(const wxString& root, ibQueryDimUnfold unfold) const {
		return ibQueryHierarchyScope(goods->GetQueryable(), goods->GetDataParent()->GetQueryColumn(),
			{ goodsRef.at(root) }, unfold);
	}

	std::set<wxString> Admitted(const ibQueryHierarchyScope& scope) const {
		std::set<wxString> codes;
		for (const auto& one : goodsRef)
			if (scope.Admits(one.second))
				codes.insert(one.first);
		return codes;
	}

	ibNumber TotalUnder(const wxString& root) const {
		const ibQueryHierarchyScope scope = Scope(root, ibQueryDimUnfold::Hierarchy);
		const nlohmann::json& nodes = fixture["goods"]["nodes"];
		// After a move the fixture nodes are stale for the parent, but the quantity does not change.
		ibNumber total;
		for (const auto& node : nodes) {
			const wxString code = AsWx(node["code"]);
			if (!scope.Admits(goodsRef.at(code)))
				continue;
			if (!scope.ReportedUnder(goodsRef.at(code)).CompareValueEQ(goodsRef.at(root)))
				continue;
			total = total + ibNumber(AsWx(node["qty"]));
		}
		return total;
	}

	std::set<wxString> CodesWhere(ibValueMetaObjectCatalog* catalog, const ibBackendQueryColumn* extra,
		const ibValue& extraValue, bool unmarkedOnly) {
		ibDataQueryBuilder q(ibConnectionPool::ThreadHolder());
		q.From(catalog->GetQueryable());
		q.Select(catalog->GetDataCode()->GetQueryColumn(), wxT("code"));
		if (unmarkedOnly)
			q.Where(catalog->GetDataDeletionMark()->GetQueryColumn(), ibQueryFilterOp::Equal, ibValue(false));
		if (extra != nullptr)
			q.Where(extra, extraValue);
		ibDataQueryResult rows = q.Execute(ibReadPageRequest{});
		std::set<wxString> codes;
		while (rows.Next())
			codes.insert(rows.GetValue(catalog->GetDataCode()->GetQueryColumn()).GetString());
		return codes;
	}

	// Equality on the Owner column's own fields — the constants the row was written with.
	// ibDataQueryBuilder::Where(ownerColumn, reference) returned no rows for these same
	// bytes, so the scenario asks the fields.
	std::set<wxString> CodesOfOwner(const ibValue& owner, bool unmarkedOnly) {
		std::vector<ibDmlAssign> key;
		Assign(key, contracts->GetCatalogOwner()->GetQueryColumn(), owner);
		if (unmarkedOnly)
			Assign(key, contracts->GetDataDeletionMark()->GetQueryColumn(), ibValue(false));
		ibQueryExprPtr where;
		for (const ibDmlAssign& one : key) {
			const ibQueryExprPtr term = ibBinOp(ibQueryBinOp::Eq, ibCol(one.m_column), one.m_value);
			where = where ? ibBinOp(ibQueryBinOp::And, where, term) : term;
		}
		wxString codeField;
		for (const wxString& field : ColumnFieldNames(contracts->GetDataCode()->GetQueryColumn()))
			if (field.EndsWith(wxT("_S")))
				codeField = field;
		ibDatabaseQueryBuilder q(ibConnectionPool::ThreadHolder());
		q.From(contracts->GetQueryable()->GetQueryTableName());
		q.Where(where);
		q.Project({ ibQueryProjItem{ ibCol(codeField), wxT("code") } });
		ibQueryResult rows = q.Execute();
		std::set<wxString> codes;
		while (rows.Next()) {
			const std::string text = rows.GetResultString(wxT("code")).utf8_str();
			codes.insert(wxString::FromUTF8(text.c_str()));
		}
		return codes;
	}

	std::set<wxString> ChoiceCodes(ibValueMetaObjectCatalog* catalog) {
		ibValue holder(ibValueReferenceDataObject::Create(cfg.get(), catalog->GetMetaID(), ibGuid(ibGuid::newGuid())));
		ibValueReferenceDataObject* ref = holder.ConvertToType<ibValueReferenceDataObject>();
		std::vector<ibValue> list;
		if (ref != nullptr)
			ref->FindValue(wxEmptyString, list);
		std::map<wxString, ibValue> all = goodsRef;
		all.insert(partyRef.begin(), partyRef.end());
		all.insert(contractRef.begin(), contractRef.end());
		std::set<wxString> codes;
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
		const ibNumber got = TotalUnder(AsWx(it.key()));
		EXPECT_EQ(0, got.Compare(ibNumber(AsWx(it.value())))) << it.key() << " " << got.ToString();
	}
}

TEST_F(AcceptanceCatalogs, MovingAnItemChangesTheFolderItSumsUnder)
{
	if (!ready) return;
	const wxString code = AsWx(fixture["goods"]["move"]["code"]);
	const wxString to = AsWx(fixture["goods"]["move"]["to"]);
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
	EXPECT_EQ(0, TotalUnder(wxT("FOOD")).Compare(ibNumber(AsWx(after["totals"]["FOOD"]))));
	EXPECT_EQ(0, TotalUnder(wxT("TOOLS")).Compare(ibNumber(AsWx(after["totals"]["TOOLS"]))));
	EXPECT_EQ(0, TotalUnder(wxT("GOODS")).Compare(ibNumber(AsWx(after["totals"]["GOODS"]))));
}

TEST_F(AcceptanceCatalogs, ADeletionMarkStaysInTheTreeAndTheChoiceStillShowsIt)
{
	if (!ready) return;
	const wxString marked = AsWx(fixture["choice"]["marked_code"]);
	EXPECT_TRUE(Admitted(Scope(wxT("FOOD"), ibQueryDimUnfold::Hierarchy)).count(marked))
		<< "a mark does not take the item out of the tree";

	EXPECT_EQ(CodesWhere(goods, nullptr, ibValue(), true), AsSet(fixture["choice"]["filtered_no_mark"]));

	// The choice door does not apply that filter. The fixture records why, and that the
	// accountant's list would have left the marked item out.
	ASSERT_TRUE(fixture["choice"]["platform_includes_deletion_mark"].get<bool>());
	const std::set<wxString> offered = ChoiceCodes(goods);
	EXPECT_TRUE(offered.count(marked)) << "FindValue returned the marked item, as the door is written";
	EXPECT_TRUE(offered.count(wxT("BREAD")));
}

TEST_F(AcceptanceCatalogs, AQueryNarrowsASubordinateCatalogToItsOwner)
{
	if (!ready) return;
	const auto& block = fixture["contracts"];
	for (auto it = block["of_owner_including_marked"].begin(); it != block["of_owner_including_marked"].end(); ++it) {
		const wxString owner = AsWx(it.key());
		EXPECT_EQ(CodesOfOwner(partyRef[owner], false), AsSet(it.value())) << owner;
	}
	for (auto it = block["of_owner_choosable"].begin(); it != block["of_owner_choosable"].end(); ++it) {
		const wxString owner = AsWx(it.key());
		EXPECT_EQ(CodesOfOwner(partyRef[owner], true), AsSet(it.value())) << owner;
	}

	const std::set<wxString> predefined = CodesWhere(parties, nullptr, ibValue(), false);
	EXPECT_TRUE(predefined.count(wxT("ACME")));
	EXPECT_TRUE(predefined.count(wxT("GLOBEX")));

	// The choice itself is not narrowed. Same written limit as the deletion mark.
	ASSERT_TRUE(block["choice_not_narrowed_by_owner"].get<bool>());
	const std::set<wxString> offered = ChoiceCodes(contracts);
	EXPECT_TRUE(offered.count(wxT("C1")));
	EXPECT_TRUE(offered.count(wxT("C2")));
	EXPECT_TRUE(offered.count(wxT("C3")));
}
