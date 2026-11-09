// =============================================================================
// OES Enterprise — issue #424: a write stores what it is given; the filling check is a question of its own.
//
// SaveData refused an object whose required fields were empty, so every Write() checked — a script that
// fills an object in two passes, an exchange that loads what another base sent, a migration: all were
// refused over a promise made to a person at a form. The check is now the object's CheckFilling(): it tells
// each empty field to the messages and answers whether all are filled, raising nothing. A write checks only
// when the object's form asks for it (ibFillCheckScope, held by the form's commands) or when it posts.
//
// Built in memory as a person builds it: a catalog with a required attribute and a tabular section with a
// required column, the values set through the object's and the section's own write.
// =============================================================================

#include <gtest/gtest.h>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/compiler/compileCode.h"
#include "backend/compiler/value.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaCollection/partial/tabularSection/tabularSection.h"
#include "backend/propertyManager/property/propertyBoolean.h"
#include "backend/session/session.h"

namespace {

struct FillCheck : ::testing::Test {
	wxInitializer                          m_wxInit;
	// RunDatabase sets this thread's code style to the configuration's; TearDown puts it back.
	const short                            m_codeStyle = ibCompileCode::GetCodeStyle();
	std::shared_ptr<ibDatabaseLayerSQLite> db;

	std::unique_ptr<ibMetaDataConfigurationFile> cfg;
	ibValueMetaObject*          warehouses = nullptr;
	ibValueMetaObjectAttribute* address    = nullptr;   // required
	ibValueMetaObject*          shelves    = nullptr;   // tabular section
	ibValueMetaObjectAttribute* capacity   = nullptr;   // required column

	ibValuePtr<ibValueRecordDataObject> object;
	ibValueRecordDataObjectRef*          ref   = nullptr;
	ibValueTabularSectionDataObjectBase* lines = nullptr;

	ibValueMetaObjectAttribute* Required(ibValueMetaObject* owner, const ibString& name, ibValueTypes type) {
		ibValueMetaObjectAttribute* attribute =
			dynamic_cast<ibValueMetaObjectAttribute*>(cfg->CreateMetaObject(g_metaAttributeCLSID, owner, /*runObject*/ false));
		if (attribute == nullptr)
			return nullptr;
		attribute->SetName(name);
		attribute->GetTypeDesc().SetDefaultMetaType(type);
		auto* fillCheck = dynamic_cast<ibPropertyBoolean*>(attribute->GetProperty(wxT("FillCheck")));
		if (fillCheck == nullptr)
			return nullptr;
		fillCheck->SetValue(true);
		return attribute;
	}

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";
		if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
			GTEST_SKIP() << "appData env unavailable headless";
		ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
		if (pool == nullptr)
			GTEST_SKIP() << "no connection pool after CreateAppDataEnv";
		db = std::make_shared<ibDatabaseLayerSQLite>();
		if (!db->Open(wxT(":memory:")))
			GTEST_SKIP() << "in-memory SQLite open failed";
		pool->Init(db, /*maxSize=*/1, /*minIdle=*/0);

		cfg = std::make_unique<ibMetaDataConfigurationFile>();
		ibValueMetaObjectConfiguration* root = cfg->GetCommonMetaObject();
		ASSERT_NE(root, nullptr);

		warehouses = cfg->CreateMetaObject(g_metaCatalogCLSID, root, false);
		ASSERT_NE(warehouses, nullptr);
		warehouses->SetName(wxT("Warehouses"));
		address = Required(warehouses, wxT("Address"), ibValueTypes::TYPE_STRING);
		ASSERT_NE(address, nullptr);
		shelves = cfg->CreateMetaObject(g_metaTableCLSID, warehouses, false);
		ASSERT_NE(shelves, nullptr);
		shelves->SetName(wxT("Shelves"));
		capacity = Required(shelves, wxT("Capacity"), ibValueTypes::TYPE_NUMBER);
		ASSERT_NE(capacity, nullptr);

		ASSERT_TRUE(cfg->RunDatabase()) << "the configuration in memory did not run";
		ASSERT_TRUE(address->FillCheck());
		ASSERT_TRUE(capacity->FillCheck());

		auto* catalog = dynamic_cast<ibValueMetaObjectRecordData*>(warehouses);
		ASSERT_NE(catalog, nullptr);
		object = catalog->CreateRecordDataObjectValue();
		ASSERT_TRUE(object);
		ref = dynamic_cast<ibValueRecordDataObjectRef*>(static_cast<ibValueRecordDataObject*>(object));
		ASSERT_NE(ref, nullptr);
		lines = dynamic_cast<ibValueTabularSectionDataObjectBase*>(object->GetTableByMetaID(shelves->GetMetaID()));
		ASSERT_NE(lines, nullptr);
	}

	void TearDown() override {
		lines = nullptr;
		ref = nullptr;
		object = ibValuePtr<ibValueRecordDataObject>();
		if (cfg) cfg->CloseDatabase();
		cfg.reset();
		ibCompileCode::SetCodeStyle(m_codeStyle);
		if (ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}
};

} // namespace

TEST_F(FillCheck, CheckFilling_NamesTheEmptyFieldsAndTheSectionAndRaisesNothing)
{
	ibSession session(wxT("fill-check"), ibSessionKind::Service);   // the messages go to a session
	ibSessionScope bound(&session);

	const ibDataViewItem line = lines->GetItem(lines->AppendRow());
	ASSERT_TRUE(line.IsOk());

	ibString unfilled;
	bool filled = true;
	EXPECT_NO_THROW(filled = ref->CheckFilling(&unfilled)) << "the check is a question, not a refusal";
	EXPECT_FALSE(filled);
	EXPECT_TRUE(unfilled.Contains(address->GetSynonym())) << unfilled.ToStdString();
	EXPECT_TRUE(unfilled.Contains(shelves->GetSynonym())) << "a line's empty column names its section: " << unfilled.ToStdString();

	ASSERT_TRUE(object->SetValueByMetaID(address->GetMetaID(), ibValue(ibString(wxT("1 Harbour Road")))));
	ASSERT_TRUE(lines->SetValueByMetaID(line, capacity->GetMetaID(), ibValue(ibNumber(40))));

	unfilled.Clear();
	ref->CheckFilling(&unfilled);
	EXPECT_FALSE(unfilled.Contains(address->GetSynonym())) << unfilled.ToStdString();
	EXPECT_FALSE(unfilled.Contains(shelves->GetSynonym())) << unfilled.ToStdString();
}

TEST_F(FillCheck, RequireFilling_RefusesWithTheFieldsNamed)
{
	ibSession session(wxT("fill-check"), ibSessionKind::Service);
	ibSessionScope bound(&session);

	try {
		ref->RequireFilling();
		ADD_FAILURE() << "an empty required field must refuse the form's write";
	}
	catch (const ibCoreException& err) {
		EXPECT_TRUE(ibString(err.GetErrorDescription()).Contains(address->GetSynonym()))
			<< err.GetErrorDescription().ToStdString();
	}
}

TEST_F(FillCheck, OnlyTheFormsWriteAsksForTheCheck)
{
	EXPECT_FALSE(ref->IsFillCheckRequired()) << "a script's Write() does not check";
	{
		const ibSourceDataObject::ibFillCheckScope outer(*ref);
		EXPECT_TRUE(ref->IsFillCheckRequired());
		{
			const ibSourceDataObject::ibFillCheckScope inner(*ref);
			EXPECT_TRUE(ref->IsFillCheckRequired());
		}
		EXPECT_TRUE(ref->IsFillCheckRequired()) << "the inner scope gives back what it found";
	}
	EXPECT_FALSE(ref->IsFillCheckRequired()) << "the form's write is over";
}
