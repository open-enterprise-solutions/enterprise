// A part of an object holds the object (#645).
//
// A tabular section speaks through its object on every write (AppendRow marks the object modified), and a
// document's RegisterRecords holder through its document (it builds its record sets from the document's
// reference). Both kept a raw pointer to the owner, so a part that outlived the owner wrote through freed
// memory: `Ref.GetObject().Goods.Add()` let go of the object as soon as `.Goods` was read.
//
// Each case below lets go of every hold on the object except the one through its part, then uses the part.
// Before the fix that was a use-after-free; the sanitizer build (OES_SANITIZE=address) names it, and a plain
// build usually crashes in AppendRow. That the object also ends — after the part, exactly once — is the
// sanitizer's to see as well: a leak or a second delete would be reported at exit.

#include <gtest/gtest.h>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/compiler/compileCode.h"
#include "backend/compiler/procUnit.h"
#include "backend/compiler/value.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/metaCollection/dimension/metaDimensionObject.h"
#include "backend/metaCollection/resource/metaResourceObject.h"
#include "backend/metaCollection/partial/accumulationRegister.h"
#include "backend/metaCollection/partial/document.h"
#include "backend/metaCollection/partial/tabularSection/tabularSection.h"
#include "core/clsid.h"

namespace {

// Reaches the protected PrepareEmptyObject — what InitializeObject does for a new object, without the
// module and the runtime around it, which these cases do not need.
class DocumentProbe : public ibValueRecordDataObjectDocument {
public:
	explicit DocumentProbe(const ibValueMetaObjectDocument* meta)
		: ibValueRecordDataObjectDocument(meta) {}
	void Prepare() { PrepareEmptyObject(); }
};

ibValue Prop(ibValue& value, const ibString& name)
{
	ibValue result;
	const long number = value.FindProp(name);
	EXPECT_NE(number, wxNOT_FOUND) << name;
	if (number != wxNOT_FOUND)
		EXPECT_TRUE(value.GetPropVal(number, result)) << name;
	return result;
}

ibValue Func(ibValue& value, const ibString& name)
{
	ibValue result;
	const long number = value.FindMethod(name);
	EXPECT_NE(number, wxNOT_FOUND) << name;
	if (number != wxNOT_FOUND)
		EXPECT_TRUE(value.CallAsFunc(number, result, nullptr, 0)) << name;
	return result;
}

struct PartHoldsWhole : ::testing::Test {
	wxInitializer                                m_wxInit;
	std::shared_ptr<ibDatabaseLayerSQLite>       db;
	std::unique_ptr<ibMetaDataConfigurationFile> cfg;
	ibValueMetaObjectDocument*                   doc = nullptr;

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

		// A document with a tabular section Goods (one string column, Item)…
		doc = dynamic_cast<ibValueMetaObjectDocument*>(cfg->CreateMetaObject(g_metaDocumentCLSID, root, false));
		ASSERT_NE(doc, nullptr);
		doc->SetName(wxT("Receipt"));
		ibValueMetaObject* goods = cfg->CreateMetaObject(g_metaTableCLSID, doc, false);
		ASSERT_NE(goods, nullptr);
		goods->SetName(wxT("Goods"));
		auto* item = dynamic_cast<ibValueMetaObjectAttribute*>(cfg->CreateMetaObject(g_metaAttributeCLSID, goods, false));
		ASSERT_NE(item, nullptr);
		item->SetName(wxT("Item"));
		item->GetTypeDesc().SetDefaultMetaType(ibValueTypes::TYPE_STRING);

		// …and an accumulation register Stock it posts to.
		auto* reg = dynamic_cast<ibValueMetaObjectAccumulationRegister*>(
			cfg->CreateMetaObject(g_metaAccumulationRegisterCLSID, root, false));
		ASSERT_NE(reg, nullptr);
		reg->SetName(wxT("Stock"));
		auto* dimension = dynamic_cast<ibValueMetaObjectDimension*>(cfg->CreateMetaObject(g_metaDimensionCLSID, reg, false));
		auto* resource = dynamic_cast<ibValueMetaObjectResource*>(cfg->CreateMetaObject(g_metaResourceCLSID, reg, false));
		ASSERT_NE(dimension, nullptr);
		ASSERT_NE(resource, nullptr);
		dimension->SetName(wxT("Item"));
		dimension->GetTypeDesc().SetDefaultMetaType(ibValueTypes::TYPE_STRING);
		resource->SetName(wxT("Quantity"));
		reg->GetRegisterRecorder()->GetTypeDesc().AppendMetaType(
			reference_to_clsid(doc->GetMetaID(), clsid_metaclass(doc->GetClassType())));
		doc->GetRecordDescription(ibRecorderWrites::Movements).AppendMetaType(reg->GetMetaID());

		ASSERT_TRUE(cfg->RunDatabase());
	}

	void TearDown() override {
		if (cfg) cfg->CloseDatabase();
		cfg.reset();
		if (ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
		if (db) db->Close();
		db.reset();
	}

	// A new object, held only by the value handed back; `raw` is for asking it things while a part holds it.
	ibValue NewObject(DocumentProbe** raw) {
		auto* object = new DocumentProbe(doc);
		ibValue held(object);
		object->Prepare();
		object->Modify(false);
		*raw = object;
		return held;
	}
};

} // namespace

// The chain from the issue: the object is a temporary, gone once `.Goods` is read; the row is added after.
TEST_F(PartHoldsWhole, ASectionTakenFromATemporaryObjectAddsARow)
{
	DocumentProbe* object = nullptr;
	ibValue goods;
	{
		ibValue temporary = NewObject(&object);
		goods = Prop(temporary, wxT("Goods"));
	}
	ASSERT_NE(goods.ConvertToType<ibValueTabularSectionDataObjectBase>(), nullptr);

	ibValue row = Func(goods, wxT("Add"));
	EXPECT_FALSE(row.IsEmpty());
	EXPECT_EQ(Func(goods, wxT("Count")).GetInteger(), 1);
	// The section's object is still the one it came from, and the row was written through to it.
	EXPECT_TRUE(object->IsModified());
}

// A function makes an object of its own and returns only its section; the caller adds a row.
TEST_F(PartHoldsWhole, ASectionReturnedFromAFunctionOutlivesTheFunctionsObject)
{
	const ibString src = ibCompileCode::GetCodeStyle() == CODE_CES
		? ibString(wxT("function TakeGoods(obj) { return obj.Goods; }\n")
		           wxT("function AddRow(goods) { goods.Add(); return goods.Count(); }\n"))
		: ibString(wxT("Function TakeGoods(Obj)\n  Return Obj.Goods;\nEndFunction\n")
		           wxT("Function AddRow(Goods)\n  Goods.Add();\n  Return Goods.Count();\nEndFunction\n"));
	ibCompileCode cc(wxT("partHoldsWhole"), wxT("memory"), false);
	ibString why;
	bool compiled = false;
	try { compiled = cc.Compile(src); }
	catch (const ibBackendException& err) { why = err.GetErrorDescription(); }
	ASSERT_TRUE(compiled) << why.ToStdString();
	ibProcUnit unit;
	unit.Execute(cc.m_cByteCode);

	DocumentProbe* object = nullptr;
	ibValue goods;
	{
		ibValue local = NewObject(&object);
		ASSERT_TRUE(unit.CallAsFunc(wxT("TakeGoods"), goods, local));
	}
	ibValue count;
	ASSERT_TRUE(unit.CallAsFunc(wxT("AddRow"), count, goods));
	EXPECT_EQ(count.GetInteger(), 1);
	EXPECT_TRUE(object->IsModified());
}

// The object puts its section aside (prepared again — as Read does) while somebody still holds the old
// one: that one stays usable, and keeps the object it was part of.
TEST_F(PartHoldsWhole, ASectionPutAsideAndStillHeldKeepsItsObject)
{
	DocumentProbe* object = nullptr;
	ibValue held = NewObject(&object);
	ibValue old = Prop(held, wxT("Goods"));
	object->Prepare();
	ibValue fresh = Prop(held, wxT("Goods"));
	ASSERT_NE(old.GetRef(), fresh.GetRef()) << "preparing again made no new section";

	held = ibValue();
	Func(old, wxT("Add"));
	EXPECT_EQ(Func(old, wxT("Count")).GetInteger(), 1);
	EXPECT_EQ(Func(fresh, wxT("Count")).GetInteger(), 0);
}

// A document's RegisterRecords holder, handed out and held after the document is let go: it still builds
// its record sets from the document, and RegisterRecords.Stock is there.
TEST_F(PartHoldsWhole, ARegisterRecordsHolderOutlivesItsDocument)
{
	DocumentProbe* object = nullptr;
	ibValue records;
	{
		ibValue temporary = NewObject(&object);
		records = ibValue(new ibValueRecordDataObjectRecorderRef::ibRecorderRegister(object));
	}
	auto* holder = records.ConvertToType<ibValueRecordDataObjectRecorderRef::ibRecorderRegister>();
	ASSERT_NE(holder, nullptr);
	holder->CreateRecordSet();   // reads the document's description and reference
	ibValue stock = Prop(records, wxT("Stock"));
	EXPECT_NE(stock.ConvertToType<ibValueRecordSetObject>(), nullptr);
	EXPECT_EQ(object->GetMetaObject(), doc);
}
