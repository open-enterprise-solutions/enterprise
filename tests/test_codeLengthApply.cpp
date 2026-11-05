// A longer code alters its column and keeps the codes stored (#646, the case #626 brought). SQLite holds any length in any
// column, so the ALTER is met where it is real: Firebird, in the job that carries the kit (its filter is `Firebird*`, and
// a skip there fails the job). Two configurations of one shape, the applied and the edited, as the apply reads them — the
// snapshot holds the live columns, so the one it was cannot be read off the one it is. The rows are the seed's: an item of
// one guid in both, and one more with the longer code in the edited.

#include <gtest/gtest.h>

#include <wx/init.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>

#include <algorithm>
#include <chrono>
#include <vector>

#include "backend/appData.h"
#include "backend/compiler/value.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/databaseResultSet.h"
#ifdef OES_USE_FIREBIRD
#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"
#endif
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metadataConfiguration.h"
#include "backend/metadataSerialization.h"   // ibSerializeValue - a declared code, as it is kept
#include "backend/query/columnLayout.h"
#include "backend/query/schemaBuilder.h"
#include "backend/query/schemaSnapshot.h"
#include "backend/restructureInfo.h"
#include "core/clsid.h"
#include "core/guid.h"

namespace {

std::shared_ptr<ibDatabaseLayer> OpenFirebird()
{
#ifdef OES_USE_FIREBIRD
	const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
	const ibString path = wxFileName(wxStandardPaths::Get().GetTempDir(),
		ibString::Format(wxT("oes_fb_code_%lld.fdb"), (long long)now)).GetFullPath();
	try {
		auto db = std::make_shared<ibDatabaseLayerFirebird>();
		db->SetUser(wxT("SYSDBA"));
		db->SetPassword(wxT("masterkey"));
		return db->Open(path) ? db : nullptr;
	}
	catch (...) {
		return nullptr;   // no client on this machine
	}
#else
	return nullptr;
#endif
}

void Settle()
{
	if (db_query->IsActiveTransaction())
		db_query->Commit();
	ibSchemaBuilder(nullptr).Flush();
	if (db_query->IsActiveTransaction())
		db_query->Commit();
}

// A value said as text, as a window types it.
ibPredefinedCellDescription Text(const ibString& text)
{
	ibPredefinedCellDescription cell;
	cell.m_title = text;
	ibSerializeValue(ibValue(text), cell.m_value);
	return cell;
}

// An item of that guid, its code and its description said as text.
ibPredefinedItemDescription Item(const ibGuid& guid, const ibString& name, const ibString& code)
{
	ibPredefinedItemDescription item;
	item.m_guid = guid;
	item.m_name = name;
	item.m_code = Text(code);
	item.m_description = Text(name);
	return item;
}

// A catalog whose code is a string of `length`.
ibValueMetaObjectRecordDataHierarchyMutableRef* Declare(ibMetaDataConfigurationFile& cfg, unsigned short length)
{
	auto* firms = dynamic_cast<ibValueMetaObjectRecordDataHierarchyMutableRef*>(
		cfg.CreateMetaObject(g_metaCatalogCLSID, cfg.GetCommonMetaObject(), false));
	if (firms == nullptr)
		return nullptr;
	firms->SetName(wxT("Firms"));
	firms->GetDataCode()->GetTypeDesc().ClearMetaType();
	firms->GetDataCode()->GetTypeDesc().SetString(length);
	return firms;
}

} // namespace

class FirebirdCodeLength : public ::testing::Test {
protected:
	void TearDown() override
	{
		if (ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}
};

TEST_F(FirebirdCodeLength, ALongerCodeAltersTheColumnAndKeepsTheCodes)
{
	wxInitializer wx;
	if (!wx.IsOk())
		GTEST_SKIP() << "wxBase init failed";
	if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
		GTEST_SKIP() << "appData env unavailable headless";
	ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
	ASSERT_NE(pool, nullptr);
	std::shared_ptr<ibDatabaseLayer> db = OpenFirebird();
	if (!db)
		GTEST_SKIP() << "no Firebird client on this machine";
	pool->Init(db, /*maxSize=*/1, /*minIdle=*/0);

	ibMetaDataConfigurationFile applied;
	ibMetaDataConfigurationFile edited;
	ibValueMetaObjectRecordDataHierarchyMutableRef* const was = Declare(applied, 8);
	ibValueMetaObjectRecordDataHierarchyMutableRef* const now = Declare(edited, 9);
	ASSERT_NE(was, nullptr);
	ASSERT_NE(now, nullptr);
	ASSERT_EQ(was->GetDataCode()->GetMetaID(), now->GetDataCode()->GetMetaID());
	ASSERT_TRUE(applied.RunDatabase());   // run, as a saved one is: its references are typed on the run
	ASSERT_TRUE(edited.RunDatabase());

	const ibGuid main = ibGuid::newGuid();
	was->SetPredefinedValue(Item(main, wxT("Main"), wxT("00000001")));
	now->SetPredefinedValue(Item(main, wxT("Main"), wxT("00000001")));
	now->SetPredefinedValue(Item(ibGuid::newGuid(), wxT("Second"), wxT("000000002")));

	const ibSchemaSnapshot before = applied.BuildSchemaSnapshot();
	ibRestructureInfo created;
	ASSERT_EQ(DiffSnapshots(nullptr, before, nullptr, &created), 1);
	Settle();

	const ibSchemaSnapshot after = edited.BuildSchemaSnapshot();
	ibRestructureInfo ledger;
	ASSERT_EQ(DiffSnapshots(&before, after, nullptr, &ledger), 1);
	EXPECT_FALSE(ledger.HasErrors());
	Settle();

	ibString field;
	for (const ibColumnSlot& slot : DescribeColumnLayout(now->GetDataCode()->GetQueryColumn()))
		if (slot.m_role == ibColumnRole::String)
			field = slot.m_name;
	ASSERT_FALSE(field.IsEmpty());

	ibDatabaseQueryBuilder query;
	query.From(now->GetPhysicalTableName());
	query.Project({ { ibCol(field), wxT("code_") } });
	std::vector<ibString> codes;
	ibQueryResult rows = query.Execute();
	while (rows.Next())
		codes.push_back(rows.GetResultString(wxT("code_")));
	ASSERT_EQ(codes.size(), 2u);
	EXPECT_NE(std::find(codes.begin(), codes.end(), ibString(wxT("00000001"))), codes.end()) << "the stored code is kept";
	EXPECT_NE(std::find(codes.begin(), codes.end(), ibString(wxT("000000002"))), codes.end()) << "the longer one fits";
}
