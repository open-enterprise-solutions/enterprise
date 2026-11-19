// A line a predefined item declares takes its place only where nothing stands (Max, 2026-11-02): the section's check
// before the update counts the lines the base holds per owner and refuses where the edited application declares a line
// over one it holds. The count is a GROUP BY the owner, and the owner is what it is read by, so the owner must be
// selected as well as grouped: it was not, and every apply that declared one line more on an item the base already held
// lines of stopped on "Field 'Row_RRRef' not found in the resultset" (2026-11-19, a demo base's chart of accounts).
//
// Two applications of one shape, the applied and the edited, as the apply reads them (test_codeLengthApply.cpp); the
// lines are the seed's.

#include <gtest/gtest.h>

#include <wx/init.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>

#include <chrono>
#include <string>

#include "backend/appData.h"
#include "backend/compiler/compileCode.h"
#include "backend/compiler/value.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/databaseResultSet.h"
#ifdef OES_USE_FIREBIRD
#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"
#endif
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metadataApplication.h"
#include "backend/metadataSerialization.h"   // ibSerializeValue - a declared value, as it is kept
#include "backend/query/columnLayout.h"      // ibOwnerRefField - the column a section's line names its owner by
#include "backend/query/provider/database/structure/schemaBuilder.h"
#include "backend/query/provider/database/structure/schemaSnapshot.h"
#include "backend/restructureInfo.h"
#include "core/clsid.h"
#include "core/guid.h"

namespace {

ibString ScratchPath(const char* engine)
{
	const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
	const char* ext = std::string(engine) == "firebird" ? "fdb" : "db";
	return wxFileName(wxStandardPaths::Get().GetTempDir(),
		ibString::Format(wxT("oes_lines_%s_%lld.%s"), engine, (long long)now, ext)).GetFullPath();
}

std::shared_ptr<ibDatabaseLayer> OpenEngine(const char* engine)
{
	if (std::string(engine) == "sqlite") {
		auto db = std::make_shared<ibDatabaseLayerSQLite>();
		return db->Open(ScratchPath(engine)) ? db : nullptr;
	}
#ifdef OES_USE_FIREBIRD
	try {
		auto db = std::make_shared<ibDatabaseLayerFirebird>();
		db->SetUser(wxT("SYSDBA"));
		db->SetPassword(wxT("masterkey"));
		return db->Open(ScratchPath(engine)) ? db : nullptr;
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

struct Shape {
	ibValueMetaObjectRecordDataHierarchyMutableRef* m_firms = nullptr;
	ibValueMetaObject* m_contacts = nullptr;   // the section
	ibValueMetaObject* m_phone = nullptr;      // its column
};

// A catalog with a section of one string column.
Shape Declare(ibMetaDataApplicationFile& cfg)
{
	Shape shape;
	shape.m_firms = dynamic_cast<ibValueMetaObjectRecordDataHierarchyMutableRef*>(
		cfg.CreateMetaObject(g_metaCatalogCLSID, cfg.GetRootMetaObject(), false));
	if (shape.m_firms == nullptr)
		return shape;
	shape.m_firms->SetName(wxT("Firms"));
	shape.m_contacts = cfg.CreateMetaObject(g_metaTableCLSID, shape.m_firms, false);
	if (shape.m_contacts == nullptr)
		return shape;
	shape.m_contacts->SetName(wxT("Contacts"));
	shape.m_phone = cfg.CreateMetaObject(g_metaAttributeCLSID, shape.m_contacts, false);
	if (shape.m_phone != nullptr)
		shape.m_phone->SetName(wxT("Phone"));
	return shape;
}

// The item Main of that guid, declaring a line per phone.
ibPredefinedItemDescription Main(const Shape& shape, const ibGuid& guid, const std::vector<ibString>& phones)
{
	ibPredefinedItemDescription item;
	item.m_guid = guid;
	item.m_name = wxT("Main");
	item.m_code = Text(wxT("000000001"));
	item.m_description = Text(wxT("Main"));
	for (const ibString& phone : phones) {
		ibPredefinedRowDescription line;
		line.emplace(shape.m_phone->GetMetaID(), Text(phone));
		item.m_tables[shape.m_contacts->GetMetaID()].push_back(line);
	}
	return item;
}

int LinesIn(const ibString& table)
{
	ibDatabaseQueryBuilder query;
	query.From(table);
	query.Project({ { ibCol(ibOwnerRefField()), wxT("owner_") } });
	int count = 0;
	ibQueryResult rows = query.Execute();
	while (rows.Next())
		++count;
	return count;
}

} // namespace

class PredefinedLinesApply : public ::testing::TestWithParam<const char*> {
protected:
	// RunDatabase sets this thread's code style to the application's; put back, as the other fixtures that run one do.
	const short m_codeStyle = ibCompileCode::GetCodeStyle();

	void TearDown() override
	{
		ibCompileCode::SetCodeStyle(m_codeStyle);
		if (ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}
};

TEST_P(PredefinedLinesApply, ALineDeclaredPastTheHeldOnesIsWrittenAndOneOverAHeldLineIsRefused)
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

	ibMetaDataApplicationFile applied;
	ibMetaDataApplicationFile edited;
	const Shape was = Declare(applied);
	const Shape now = Declare(edited);
	ASSERT_NE(was.m_phone, nullptr);
	ASSERT_NE(now.m_phone, nullptr);
	ASSERT_EQ(was.m_contacts->GetMetaID(), now.m_contacts->GetMetaID());
	ASSERT_TRUE(applied.RunDatabase());   // run, as a saved one is: its references are typed on the run
	ASSERT_TRUE(edited.RunDatabase());

	const ibGuid main = ibGuid::newGuid();
	was.m_firms->SetPredefinedValue(Main(was, main, { wxT("100") }));
	now.m_firms->SetPredefinedValue(Main(now, main, { wxT("100"), wxT("200") }));

	const ibSchemaSnapshot before = applied.BuildSchemaSnapshot();
	ibRestructureInfo created;
	ASSERT_EQ(DiffSnapshots(nullptr, before, nullptr, &created), 1);
	Settle();
	const ibSchemaTable* section = before.Find(was.m_contacts->GetMetaID());
	ASSERT_NE(section, nullptr);
	ASSERT_EQ(LinesIn(section->m_name), 1);

	// One line more than the applied application declared, and the base holds only that one: the check counts the held
	// lines by owner, passes, and the second line is written.
	const ibSchemaSnapshot after = edited.BuildSchemaSnapshot();
	ibRestructureInfo ledger;
	ASSERT_EQ(DiffSnapshots(&before, after, nullptr, &ledger), 1);
	EXPECT_FALSE(ledger.HasErrors()) << (ledger.HasErrors() ? ledger.At(0).descr.ToStdString() : std::string());
	Settle();
	EXPECT_EQ(LinesIn(section->m_name), 2);

	// ⭐ Asked as if nothing had been declared before, the base's two lines stand where the item declares its first: the
	// check refuses, naming the line.
	const ibSchemaTable* lines = after.Find(now.m_contacts->GetMetaID());
	ASSERT_NE(lines, nullptr);
	ibRestructureInfo refused;
	EXPECT_FALSE(lines->m_beforeChange(nullptr, &refused));
	ASSERT_TRUE(refused.HasErrors());
	EXPECT_TRUE(refused.At(0).descr.Contains(wxT("Main.Contacts 1"))) << refused.At(0).descr.ToStdString();
}

INSTANTIATE_TEST_SUITE_P(Engines, PredefinedLinesApply, ::testing::Values("sqlite", "firebird"));
