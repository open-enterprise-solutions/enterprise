// A totals rebuild is decided from the declared type, not the SQL layout. CatalogRef.A|B|C and
// A|B occupy the same reference pair, and the differ still clears every C. A wider string, or a
// type that was added, clears nothing.

#include <gtest/gtest.h>

#include <wx/init.h>

#include "backend/compiler/value.h"
#include "backend/query/columnLayout.h"
#include "backend/query/derivedStateBuilder.h"
#include "backend/query/queryable.h"
#include "backend/query/schemaSnapshot.h"
#include "core/clsid.h"

namespace {

struct TypedColumn : ibBackendQueryColumn {
	wxString physical;
	ibMetaID id = 1;
	mutable ibTypeDescription type;

	wxString GetName() const override { return physical; }
	wxString GetPhysicalName() const override { return physical; }
	ibTypeDescription& GetTypeDesc() const override { return type; }
	ibMetaID GetColumnId() const override { return id; }
};

struct TotalsTypeLoss : ::testing::Test {
	wxInitializer wx;
};

void ExpectSameLayout(const ibBackendQueryColumn& was, const ibBackendQueryColumn& now)
{
	const std::vector<ibColumnSlot> oldSlots = DescribeColumnLayout(&was);
	const std::vector<ibColumnSlot> newSlots = DescribeColumnLayout(&now);
	ASSERT_EQ(oldSlots.size(), newSlots.size());
	for (size_t i = 0; i < oldSlots.size(); ++i) {
		EXPECT_EQ(oldSlots[i].m_name, newSlots[i].m_name);
		EXPECT_TRUE(ibSameFieldType(oldSlots[i].m_type, newSlots[i].m_type));
	}
}

ibSchemaTable Derived(const ibBackendQueryColumn* key)
{
	ibSchemaTable table;
	table.m_derived = true;
	table.m_materialize.Key(key);
	return table;
}

} // namespace

TEST_F(TotalsTypeLoss, ARemovedReferenceTargetOnTheKeyRebuilds)
{
	TypedColumn was;
	was.physical = wxT("goods");
	was.id = 7;
	was.type.AppendMetaType(reference_to_clsid(1));
	was.type.AppendMetaType(reference_to_clsid(2));
	was.type.AppendMetaType(reference_to_clsid(3));
	TypedColumn now = was;
	now.type.ClearMetaType(reference_to_clsid(3));
	ExpectSameLayout(was, now);

	const ibSchemaTable oldTable = Derived(&was);
	const ibSchemaTable newTable = Derived(&now);
	EXPECT_TRUE(ibDerivedState::NeedsRegeneration(&oldTable, newTable));
}

TEST_F(TotalsTypeLoss, ARemovedReferenceTargetInTheContributionRebuilds)
{
	TypedColumn key;
	key.physical = wxT("period");
	key.id = 1;
	key.type.AppendMetaType(ibValueTypes::TYPE_NUMBER);

	TypedColumn was;
	was.physical = wxT("qty");
	was.id = 9;
	was.type.AppendMetaType(reference_to_clsid(1));
	was.type.AppendMetaType(reference_to_clsid(2));
	was.type.AppendMetaType(reference_to_clsid(3));
	TypedColumn now = was;
	now.type.ClearMetaType(reference_to_clsid(3));
	ExpectSameLayout(was, now);

	ibSchemaTable oldTable = Derived(&key);
	ibSchemaTable newTable = Derived(&key);
	oldTable.m_materialize.Accumulate(nullptr, wxString(), ibQueryColumnExpr::Col(&was));
	newTable.m_materialize.Accumulate(nullptr, wxString(), ibQueryColumnExpr::Col(&now));
	EXPECT_TRUE(ibDerivedState::NeedsRegeneration(&oldTable, newTable));
}

TEST_F(TotalsTypeLoss, ARemovedReferenceTargetInTheGuardRebuilds)
{
	TypedColumn key;
	key.physical = wxT("period");
	key.id = 1;
	key.type.AppendMetaType(ibValueTypes::TYPE_NUMBER);

	TypedColumn was;
	was.physical = wxT("active");
	was.id = 4;
	was.type.AppendMetaType(reference_to_clsid(1));
	was.type.AppendMetaType(reference_to_clsid(2));
	TypedColumn now = was;
	now.type.ClearMetaType(reference_to_clsid(2));

	ibQueryCondition oldCond;
	oldCond.m_col = &was;
	ibQueryCondition newCond;
	newCond.m_col = &now;

	ibSchemaTable oldTable = Derived(&key);
	ibSchemaTable newTable = Derived(&key);
	oldTable.m_materialize.m_guardExpr = ibQueryPredicate::Leaf(oldCond);
	newTable.m_materialize.m_guardExpr = ibQueryPredicate::Leaf(newCond);
	EXPECT_TRUE(ibDerivedState::NeedsRegeneration(&oldTable, newTable));
}

TEST_F(TotalsTypeLoss, AWiderStringClearsNothing)
{
	TypedColumn key;
	key.physical = wxT("period");
	key.id = 1;
	key.type.AppendMetaType(ibValueTypes::TYPE_NUMBER);

	TypedColumn was;
	was.physical = wxT("code");
	was.id = 3;
	was.type.AppendMetaType(ibValueTypes::TYPE_STRING);
	was.type.m_typeData.SetString(10);
	TypedColumn now = was;
	now.type.m_typeData.SetString(20);

	ibSchemaTable oldTable = Derived(&key);
	ibSchemaTable newTable = Derived(&key);
	oldTable.m_materialize.Accumulate(nullptr, wxString(), ibQueryColumnExpr::Col(&was));
	newTable.m_materialize.Accumulate(nullptr, wxString(), ibQueryColumnExpr::Col(&now));
	EXPECT_FALSE(ibDerivedState::NeedsRegeneration(&oldTable, newTable));
}

TEST_F(TotalsTypeLoss, AnAddedTypeClearsNothing)
{
	TypedColumn key;
	key.physical = wxT("period");
	key.id = 1;
	key.type.AppendMetaType(ibValueTypes::TYPE_NUMBER);

	TypedColumn was;
	was.physical = wxT("qty");
	was.id = 3;
	was.type.AppendMetaType(ibValueTypes::TYPE_NUMBER);
	TypedColumn now = was;
	now.type.AppendMetaType(ibValueTypes::TYPE_STRING);

	ibSchemaTable oldTable = Derived(&key);
	ibSchemaTable newTable = Derived(&key);
	oldTable.m_materialize.Accumulate(nullptr, wxString(), ibQueryColumnExpr::Col(&was));
	newTable.m_materialize.Accumulate(nullptr, wxString(), ibQueryColumnExpr::Col(&now));
	EXPECT_FALSE(ibDerivedState::NeedsRegeneration(&oldTable, newTable));
}

// =============================================================================
// The apply's own diff, on a live engine. DiffSnapshots is the call OnSaveDatabase makes
// once ApplyConfiguration has the two snapshots. Driving that verb twice from a headless
// fixture does not get there: the configuration reloaded from the blob describes the
// catalog's own reference with an _RTRef the first apply never created, and the second
// apply stops on that column. Nothing was edited between the two. The diff below is the
// restructuring itself, with both snapshots still in memory and the meta ids the same.
//
// Several lines of one recorder, then the dimension loses a catalog. The totals table is
// rebuilt, and each key's figure is the sum of the movements under that key afterwards —
// including the line whose discriminator the clear set to empty.
// =============================================================================

#include <chrono>
#include <cstdlib>
#include <map>
#include <string>

#include <wx/filename.h>
#include <wx/stdpaths.h>

#include "backend/appData.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/databaseResultSet.h"
#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"
#include "backend/databaseLayer/postgres/postgresDatabaseLayer.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metaCollection/dimension/metaDimensionObject.h"
#include "backend/metaCollection/partial/accumulationRegister.h"
#include "backend/metaCollection/resource/metaResourceObject.h"
#include "backend/metadataConfiguration.h"
#include "backend/propertyManager/property/propertyBoolean.h"
#include "backend/query/schemaBuilder.h"
#include "backend/restructureInfo.h"
#include "core/guid.h"

namespace {

wxString EnvOr(const char* name, const wxString& fallback)
{
	const char* value = std::getenv(name);
	return (value != nullptr && *value != '\0') ? wxString::FromUTF8(value) : fallback;
}

wxString ScratchPath(const char* engine)
{
	const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
	const char* ext = std::string(engine) == "firebird" ? "fdb" : "db";
	return wxFileName(wxStandardPaths::Get().GetTempDir(),
		wxString::Format(wxT("oes_totals_%s_%lld.%s"), engine, (long long)now, ext)).GetFullPath();
}

std::shared_ptr<ibDatabaseLayer> OpenEngine(const char* engine)
{
	if (std::string(engine) == "sqlite") {
		auto db = std::make_shared<ibDatabaseLayerSQLite>();
		return db->Open(ScratchPath(engine)) ? db : nullptr;
	}
	if (std::string(engine) == "postgres") {
		if (std::getenv("OES_PG_USER") == nullptr || *std::getenv("OES_PG_USER") == '\0')
			return nullptr;
		auto db = std::make_shared<ibDatabaseLayerPostgres>();
		if (!db->Open(EnvOr("OES_PG_HOST", wxT("127.0.0.1")),
		              EnvOr("OES_PG_PORT", wxT("5432")),
		              wxT("oes_totals"),
		              EnvOr("OES_PG_USER", wxT("postgres")),
		              EnvOr("OES_PG_PASSWORD", wxEmptyString)))
			return nullptr;
		db->RunQuery(wxT("%s"), wxT("DROP SCHEMA IF EXISTS public CASCADE"));
		db->RunQuery(wxT("%s"), wxT("CREATE SCHEMA public"));
		if (db->IsActiveTransaction())
			db->Commit();
		return db;
	}
	try {
		auto db = std::make_shared<ibDatabaseLayerFirebird>();
		db->SetUser(wxT("SYSDBA"));
		db->SetPassword(wxT("masterkey"));
		if (!db->Open(ScratchPath(engine)))
			return nullptr;
		return db;
	}
	catch (...) {
		return nullptr;
	}
}

void Settle()
{
	if (db_query->IsActiveTransaction())
		db_query->Commit();
	ibSchemaBuilder(nullptr).Flush();
	if (db_query->IsActiveTransaction())
		db_query->Commit();
}

bool FindSlot(const ibBackendQueryColumn* col, ibColumnRole role, ibColumnSlot& out)
{
	for (const ibColumnSlot& slot : DescribeColumnLayout(col))
		if (slot.m_role == role) {
			out = slot;
			return true;
		}
	return false;
}

void Put(std::vector<ibDmlAssign>& row, const wxString& name, const ibValue& value)
{
	row.push_back({ name, ibConst(value) });
}

void PutRef(std::vector<ibDmlAssign>& row, const ibBackendQueryColumn* col, ibClassID clsid, const ibGuid& id)
{
	for (const ibColumnSlot& slot : DescribeColumnLayout(col)) {
		if (slot.m_role == ibColumnRole::Discriminator)
			Put(row, slot.m_name, ibValue(static_cast<int>(ibFieldTypes_Reference)));
		else if (slot.m_role == ibColumnRole::ReferenceType)
			Put(row, slot.m_name, ibValue(ibNumber(static_cast<unsigned long long>(clsid))));
		else if (slot.m_role == ibColumnRole::ReferenceId)
			row.push_back({ slot.m_name, ibConstBlob(id.bytes().data(), id.bytes().size()) });
	}
}

void PutTagged(std::vector<ibDmlAssign>& row, const ibBackendQueryColumn* col, int tag, const ibValue& value)
{
	for (const ibColumnSlot& slot : DescribeColumnLayout(col)) {
		if (slot.m_role == ibColumnRole::Discriminator)
			Put(row, slot.m_name, ibValue(tag));
		else if (slot.m_role == ibColumnRole::Number || slot.m_role == ibColumnRole::Raw
		         || slot.m_role == ibColumnRole::Enum || slot.m_role == ibColumnRole::Date
		         || slot.m_role == ibColumnRole::Boolean)
			Put(row, slot.m_name, value);
	}
}

struct RegisterShape {
	ibValueMetaObject* catalogA = nullptr;
	ibValueMetaObject* catalogB = nullptr;
	ibValueMetaObject* document = nullptr;
	ibValueMetaObjectAccumulationRegister* reg = nullptr;
	ibValueMetaObjectDimension* dimension = nullptr;
	ibValueMetaObjectResource* resource = nullptr;
	ibClassID refA = 0;
	ibClassID refB = 0;
	ibClassID refDoc = 0;
};

RegisterShape Declare(ibMetaDataConfigurationFile& cfg)
{
	RegisterShape shape;
	ibValueMetaObjectConfiguration* root = cfg.GetCommonMetaObject();
	shape.catalogA = cfg.CreateMetaObject(g_metaCatalogCLSID, root, false);
	shape.catalogB = cfg.CreateMetaObject(g_metaCatalogCLSID, root, false);
	shape.document = cfg.CreateMetaObject(g_metaDocumentCLSID, root, false);
	shape.reg = dynamic_cast<ibValueMetaObjectAccumulationRegister*>(
		cfg.CreateMetaObject(g_metaAccumulationRegisterCLSID, root, false));
	if (shape.reg == nullptr)
		return shape;
	shape.dimension = dynamic_cast<ibValueMetaObjectDimension*>(
		cfg.CreateMetaObject(g_metaDimensionCLSID, shape.reg, false));
	shape.resource = dynamic_cast<ibValueMetaObjectResource*>(
		cfg.CreateMetaObject(g_metaResourceCLSID, shape.reg, false));
	if (shape.catalogA == nullptr || shape.catalogB == nullptr || shape.document == nullptr
	    || shape.dimension == nullptr || shape.resource == nullptr)
		return shape;

	shape.catalogA->SetName(wxT("Goods"));
	shape.catalogB->SetName(wxT("Services"));
	shape.document->SetName(wxT("Sale"));
	shape.reg->SetName(wxT("Stock"));
	shape.dimension->SetName(wxT("Item"));
	shape.resource->SetName(wxT("Qty"));
	if (ibPropertyBoolean* split = dynamic_cast<ibPropertyBoolean*>(shape.reg->GetProperty(wxT("SplitTotals"))))
		split->SetValue(false);

	shape.refA = reference_to_clsid(shape.catalogA->GetMetaID(), clsid_metaclass(shape.catalogA->GetClassType()));
	shape.refB = reference_to_clsid(shape.catalogB->GetMetaID(), clsid_metaclass(shape.catalogB->GetClassType()));
	shape.refDoc = reference_to_clsid(shape.document->GetMetaID(), clsid_metaclass(shape.document->GetClassType()));
	shape.dimension->GetTypeDesc().ClearMetaType();
	shape.dimension->GetTypeDesc().AppendMetaType(shape.refA);
	shape.dimension->GetTypeDesc().AppendMetaType(shape.refB);
	shape.resource->GetTypeDesc().SetDefaultMetaType(ibValueTypes::TYPE_NUMBER);
	shape.reg->GetRegisterRecorder()->GetTypeDesc().AppendMetaType(shape.refDoc);
	return shape;
}

wxString HexOf(const wxMemoryBuffer& buffer)
{
	wxString hex;
	const auto* bytes = static_cast<const unsigned char*>(buffer.GetData());
	for (size_t i = 0; i < buffer.GetDataLen(); ++i)
		hex += wxString::Format(wxT("%02x"), bytes[i]);
	return hex;
}

struct KeySum {
	ibNumber m_in;
	ibNumber m_out;
};

std::map<wxString, KeySum> ReadFigures(const wxString& table, const wxString& tag, const wxString& target,
	const wxString& guid, const wxString& inColumn, const wxString& outColumn)
{
	ibDatabaseQueryBuilder query;
	query.From(table);
	std::vector<ibQueryProjItem> projection{
		{ ibCol(tag), wxT("tag_") },
		{ ibCol(target), wxT("target_") },
		{ ibCol(guid), wxT("guid_") },
	};
	if (!inColumn.IsEmpty())
		projection.push_back({ ibCol(inColumn), wxT("in_") });
	if (!outColumn.IsEmpty())
		projection.push_back({ ibCol(outColumn), wxT("out_") });
	query.Project(projection);

	std::map<wxString, KeySum> sums;
	ibQueryResult rows = query.Execute();
	while (rows.Next()) {
		wxMemoryBuffer id;
		if (!rows.IsResultNull(wxT("guid_")))
			rows.GetResultBlob(wxT("guid_"), id);
		// _RTRef is a BIGINT. Firebird's reader refuses a number asked for as text
		// ("Invalid field type"); the codec reads this slot with GetResultLong.
		wxString targetText;
		if (!rows.IsResultNull(wxT("target_")))
			targetText = wxString::Format(wxT("%lld"), rows.GetResultLong(wxT("target_")));
		const wxString key = wxString::Format(wxT("%lld|%s|%s"),
			rows.IsResultNull(wxT("tag_")) ? 0LL : rows.GetResultLong(wxT("tag_")),
			targetText, HexOf(id));
		KeySum& sum = sums[key];
		if (!inColumn.IsEmpty() && !rows.IsResultNull(wxT("in_")))
			sum.m_in = sum.m_in + rows.GetResultNumber(wxT("in_"));
		if (!outColumn.IsEmpty() && !rows.IsResultNull(wxT("out_")))
			sum.m_out = sum.m_out + rows.GetResultNumber(wxT("out_"));
	}
	return sums;
}

void DropTriggers(const char* engine, const wxString& movements, const wxString& totals)
{
	const wxString suffixes[] = { wxT("_AI"), wxT("_AU"), wxT("_AD") };
	for (const wxString& suffix : suffixes) {
		const wxString name = totals + suffix;
		try {
			if (std::string(engine) == "postgres")
				db_query->RunQuery(wxT("%s"), wxString::Format(wxT("DROP TRIGGER IF EXISTS %s ON %s"), name, movements));
			else if (std::string(engine) == "firebird")
				db_query->RunQuery(wxT("%s"), wxString::Format(wxT("DROP TRIGGER %s"), name));
			else
				db_query->RunQuery(wxT("%s"), wxString::Format(wxT("DROP TRIGGER IF EXISTS %s"), name));
		}
		catch (...) {}
	}
}

} // namespace

class TotalsTypeLossApply : public ::testing::TestWithParam<const char*> {
protected:
	void TearDown() override
	{
		if (ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}
};

TEST_P(TotalsTypeLossApply, ADimensionThatLosesATypeRebuildsTotalsToTheMovementSum)
{
	wxInitializer wx;
	if (!wx.IsOk())
		GTEST_SKIP() << "wxBase init failed";
	if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
		GTEST_SKIP() << "appData env unavailable headless";
	ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
	ASSERT_NE(pool, nullptr);
	std::shared_ptr<ibDatabaseLayer> db = OpenEngine(GetParam());
	if (!db)
		GTEST_SKIP() << GetParam() << " is not available";
	pool->Init(db, /*maxSize=*/1, /*minIdle=*/0);

	ibMetaDataConfigurationFile wideCfg;
	ibMetaDataConfigurationFile narrowCfg;
	const RegisterShape wide = Declare(wideCfg);
	const RegisterShape narrow = Declare(narrowCfg);
	ASSERT_NE(wide.reg, nullptr);
	ASSERT_NE(narrow.dimension, nullptr);
	ASSERT_EQ(wide.dimension->GetMetaID(), narrow.dimension->GetMetaID());
	narrow.dimension->GetTypeDesc().ClearMetaType(narrow.refB);

	const ibSchemaSnapshot wideSnap = wideCfg.BuildSchemaSnapshot();
	ibRestructureInfo created;
	ASSERT_EQ(DiffSnapshots(nullptr, wideSnap, nullptr, &created), 1);
	Settle();

	const ibSchemaTable* totals = nullptr;
	for (const ibSchemaTable& table : wideSnap.Tables())
		if (table.m_derived && !table.m_materialize.m_deltas.empty())
			totals = &table;
	ASSERT_NE(totals, nullptr);
	const ibSchemaTable* movements = nullptr;
	for (const ibSchemaTable& table : wideSnap.Tables())
		if (!table.m_derived && table.m_name == totals->m_materialize.SourceTable())
			movements = &table;
	ASSERT_NE(movements, nullptr);
	DropTriggers(GetParam(), movements->m_name, totals->m_name);

	ibColumnSlot tag, target, guidCol;
	ASSERT_TRUE(FindSlot(wide.dimension->GetQueryColumn(), ibColumnRole::Discriminator, tag));
	ASSERT_TRUE(FindSlot(wide.dimension->GetQueryColumn(), ibColumnRole::ReferenceType, target));
	ASSERT_TRUE(FindSlot(wide.dimension->GetQueryColumn(), ibColumnRole::ReferenceId, guidCol));
	wxString inColumn, outColumn;
	for (const ibSchemaDelta& delta : totals->m_materialize.m_deltas) {
		const wxString name = delta.m_column->GetPhysicalName();
		if (name.EndsWith(wxT("_In")))
			inColumn = name;
		else if (name.EndsWith(wxT("_Out")))
			outColumn = name;
	}
	ASSERT_FALSE(inColumn.IsEmpty());

	const ibGuid goods = ibGuid::newGuid();
	const ibGuid service = ibGuid::newGuid();
	const ibGuid recorder = ibGuid::newGuid();
	const ibDateTime day(2026, 3, 2);
	const int receipt = static_cast<int>(ibRecordType::eReceipt);
	const auto line = [&](int number, ibClassID kind, const ibGuid& id, long long qty) {
		std::vector<ibDmlAssign> row;
		for (const ibSchemaColumn& column : movements->m_columns) {
			const ibBackendQueryColumn* field = column.m_column;
			if (field == nullptr)
				continue;
			const ibMetaID idOf = field->GetColumnId();
			if (idOf == wide.dimension->GetMetaID())
				PutRef(row, field, kind, id);
			else if (idOf == wide.resource->GetMetaID())
				PutTagged(row, field, static_cast<int>(ibFieldTypes_Number), ibValue(ibNumber(qty)));
			else if (idOf == wide.reg->GetRegisterRecorder()->GetMetaID())
				PutRef(row, field, wide.refDoc, recorder);
			else if (idOf == wide.reg->GetRegisterLineNumber()->GetMetaID())
				PutTagged(row, field, static_cast<int>(ibFieldTypes_Number), ibValue(ibNumber(static_cast<long long>(number))));
			else if (idOf == wide.reg->GetRegisterPeriod()->GetMetaID())
				PutTagged(row, field, static_cast<int>(ibFieldTypes_Date), ibValue(day));
			else if (idOf == wide.reg->GetRegisterActive()->GetMetaID())
				PutTagged(row, field, static_cast<int>(ibFieldTypes_Boolean), ibValue(true));
			else if (wide.reg->GetRegisterRecordType() != nullptr && idOf == wide.reg->GetRegisterRecordType()->GetMetaID())
				PutTagged(row, field, static_cast<int>(ibFieldTypes_Enum), ibValue(ibNumber(static_cast<long long>(receipt))));
		}
		ASSERT_EQ(ibDatabaseQueryBuilder().Execute(ibInsert(movements->m_name, row)), 1);
	};
	line(1, wide.refA, goods, 12);
	line(2, wide.refA, goods, 8);
	line(3, wide.refB, service, 4);
	if (db_query->IsActiveTransaction())
		db_query->Commit();

	const ibSchemaSnapshot narrowSnap = narrowCfg.BuildSchemaSnapshot();
	ibRestructureInfo ledger;
	ASSERT_EQ(DiffSnapshots(&wideSnap, narrowSnap, nullptr, &ledger), 1);
	Settle();

	bool rebuilt = false;
	for (const ibRestructureInfo::Entry& entry : ledger)
		if (entry.descr.Find(wxT("Rebuild totals")) != wxNOT_FOUND)
			rebuilt = true;
	EXPECT_TRUE(rebuilt) << "a removed reference target shares the SQL layout and still rebuilds the totals";

	ibColumnSlot qtySlot;
	if (!FindSlot(wide.resource->GetQueryColumn(), ibColumnRole::Number, qtySlot)) {
		ASSERT_TRUE(FindSlot(wide.resource->GetQueryColumn(), ibColumnRole::Raw, qtySlot));
	}
	const std::map<wxString, KeySum> movement = ReadFigures(movements->m_name, tag.m_name, target.m_name, guidCol.m_name,
		qtySlot.m_name, wxString());
	const std::map<wxString, KeySum> total = ReadFigures(totals->m_name, tag.m_name, target.m_name, guidCol.m_name, inColumn, outColumn);

	ASSERT_EQ(movement.size(), 2u);
	ibNumber moved;
	bool sawEmpty = false;
	for (const auto& one : movement) {
		moved = moved + one.second.m_in;
		const auto found = total.find(one.first);
		ASSERT_NE(found, total.end()) << "totals have no row for movement key " << one.first.ToStdString();
		EXPECT_EQ(found->second.m_in, one.second.m_in);
		EXPECT_EQ(found->second.m_out, ibNumber());
		if (one.first.StartsWith(wxT("0|")))
			sawEmpty = true;
	}
	EXPECT_TRUE(sawEmpty);
	EXPECT_EQ(moved, ibNumber(24L));
}

INSTANTIATE_TEST_SUITE_P(Engines, TotalsTypeLossApply, ::testing::Values("sqlite", "postgres", "firebird"));
