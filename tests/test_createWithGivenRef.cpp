// =============================================================================
// A NEW OBJECT WRITTEN UNDER A REFERENCE GIVEN - what a load of data that already has an identity asks for.
//
// Two catalogs and a document in an application in memory, on SQLite, their tables made of exactly the
// fields the metaobject lays out. Everything goes the script's road: the manager's GetRef and creators
// called as a module calls them, the object's Write, the row read back by the reference.
//
// What it pins: GetRef(id) is the reference of the manager's type with that identifier and reads nothing;
// a creator given a reference makes an ordinary NEW object whose Write inserts the row under that
// reference; a creator given nothing mints an identity as before; and a reference that is taken, empty,
// of another type, or no reference at all is refused by the creator, the object named.
// =============================================================================

#include <gtest/gtest.h>
#include <wx/string.h>   // wxT, wxString - the engine's headers include no wx

#include <memory>
#include <set>
#include <vector>

#include <wx/init.h>

#include "activeMetaDataAccess.h"
#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/compiler/compileCode.h"
#include "backend/compiler/value.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metadataApplication.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/metaRoleObject.h"
#include "backend/metaCollection/partial/catalog.h"
#include "backend/metaCollection/partial/document.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/objCtor.h"
#include "backend/query/columnLayout.h"
#include "backend/query/dataQueryBuilder.h"
#include "backend/session/session.h"
#include "backend/session/sessionHolder.h"
#include "backend/session/sessionRegistry.h"
#include "backend/system/value/valueGuid.h"
#include "backend/userInfo.h"

namespace {

const ibString kKnownId = wxT("3f2504e0-4f89-41d3-9a0c-0305e82c3301");
const ibString kOtherId = wxT("3f2504e0-4f89-41d3-9a0c-0305e82c3302");

struct CreateWithGivenRefFix : ::testing::Test {
	wxInitializer                          m_wxInit;
	// RunDatabase sets this thread's code style to the application's; TearDown puts it back.
	const short                            m_codeStyle = ibCompileCode::GetCodeStyle();
	std::shared_ptr<ibDatabaseLayerSQLite> db;

	std::shared_ptr<ibMetaDataApplicationFile> cfg;
	ibValueMetaObjectCatalog*  products = nullptr;
	ibValueMetaObjectCatalog*  units    = nullptr;   // another type: its reference is no reference of Products
	ibValueMetaObjectDocument* order    = nullptr;
	ibValueMetaObjectRole*     role     = nullptr;
	std::shared_ptr<ibSession> session;

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

		cfg = ibMetaData::MakeShared<ibMetaDataApplicationFile>();
		ActiveMetaDataAccess::Replace(cfg);
		ibValueMetaObjectApplication* root = cfg->GetRootMetaObject();
		ASSERT_NE(root, nullptr);
		products = dynamic_cast<ibValueMetaObjectCatalog*>(cfg->CreateMetaObject(g_metaCatalogCLSID, root, false));
		units    = dynamic_cast<ibValueMetaObjectCatalog*>(cfg->CreateMetaObject(g_metaCatalogCLSID, root, false));
		order    = dynamic_cast<ibValueMetaObjectDocument*>(cfg->CreateMetaObject(g_metaDocumentCLSID, root, false));
		role     = dynamic_cast<ibValueMetaObjectRole*>(cfg->CreateMetaObject(g_metaRoleCLSID, root, false));
		ASSERT_NE(products, nullptr);
		ASSERT_NE(units, nullptr);
		ASSERT_NE(order, nullptr);
		ASSERT_NE(role, nullptr);
		products->SetName(wxT("Products"));
		units->SetName(wxT("Units"));
		order->SetName(wxT("Order"));
		role->SetName(wxT("Loader"));
		// A role with administration grants everything and filters nothing.
		ASSERT_TRUE(root->SetRight(wxT("Administration"), role->GetMetaID(), false));

		ASSERT_TRUE(cfg->RunDatabase()) << "the application in memory did not run";

		for (const ibValueMetaObjectRecordDataMutableRef* object :
			std::vector<const ibValueMetaObjectRecordDataMutableRef*>{ products, units, order })
			db->RunQuery(wxT("%s"), wxT("CREATE TABLE ") + object->GetQueryable()->GetQueryTableName()
				+ wxT(" (") + FieldList(object) + wxT(")"));

		session = SessionInRole();
		ASSERT_NE(session, nullptr);
	}
	void TearDown() override {
		if (session) {
			ibSessionScope bound(session.get());
			session->ClearRoot();
		}
		session.reset();
		if (cfg) cfg->CloseDatabase();
		cfg.reset();
		ibCompileCode::SetCodeStyle(m_codeStyle);
		if (ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}

	// Every field a read of the object selects, declared by what it holds, as the platform's own DDL declares them.
	static ibString FieldList(const ibValueMetaObjectRecordDataMutableRef* object) {
		ibString list;
		std::set<ibString> seen;
		for (const std::shared_ptr<const ibBackendQueryColumn>& column : object->GetQueryable()->GetQueryColumns())
			for (const ibString& field : ColumnFieldNames(*column))
				if (seen.insert(field).second)
					list << (list.IsEmpty() ? wxT("") : wxT(", ")) << field << SqliteTypeOf(field);
		return list;
	}
	static ibString SqliteTypeOf(const ibString& field) {
		if (field.EndsWith(wxT("_RRRef"))) return wxT(" BLOB");
		if (field.EndsWith(wxT("_D")) || field.EndsWith(wxT("_S"))) return wxT(" TEXT");
		if (field.EndsWith(wxT("_N"))) return wxT(" NUMERIC");
		return wxT(" INTEGER");   // _TYPE, _RTRef, _B
	}

	// A session of a user in the role, its runtime compiled: an object's module runs in one.
	std::shared_ptr<ibSession> SessionInRole() {
		auto made = std::make_shared<ibSession>(wxT("loader"), ibSessionKind::Workspace);
		ibUserInfo user;
		user.m_strUserGuid = wxT("22222222-2222-2222-2222-222222222222");
		user.m_strUserName = wxT("loader");
		ibUserInfo::ibUserRole membership;
		membership.m_miRoleId = role->GetMetaID();
		membership.m_strRoleName = wxT("Loader");
		user.m_roleArray.push_back(membership);
		ibSessionRegistry* registry = ibApplicationInstance::GetSessionRegistry();
		if (registry == nullptr)
			return nullptr;
		registry->InstallUser(made.get(), user, wxEmptyString);

		ibSessionScope bound(made.get());
		if (made->CreateRoot(cfg.get()) == nullptr)
			return nullptr;
		try {
			if (!made->CompileRoot())
				return nullptr;
		}
		catch (const ibBackendException& err) {
			ADD_FAILURE() << err.GetErrorDescription().ToStdString();
			return nullptr;
		}
		return made;
	}

	// What code reaches as `Catalogs.Products` / `Documents.Order`.
	static ibValue Manager(const ibValueMetaObjectRecordDataMutableRef* of) {
		const ibCtorMetaValueType* managerType = of->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Manager);
		return managerType != nullptr ? ibValue(managerType->CreateObject()) : ibValue();
	}

	// A function of a value called as a module calls it: by name, with what was passed.
	static ibValue Call(ibValue on, const ibString& name, std::vector<ibValue> passed = {}) {
		const long method = on.FindMethod(name);
		if (method == wxNOT_FOUND) {
			ADD_FAILURE() << "no method " << name.ToStdString();
			return ibValue();
		}
		std::vector<ibValue*> frame;
		for (ibValue& value : passed)
			frame.push_back(&value);
		// The call frame is as wide as the method declares: a parameter not passed is an empty slot.
		ibValue notPassed;
		while (static_cast<long>(frame.size()) < on.GetNParams(method))
			frame.push_back(&notPassed);
		ibValue result;
		on.CallAsFunc(method, result, frame.empty() ? nullptr : frame.data(), static_cast<long>(passed.size()));
		return result;
	}
	static void Set(ibValue on, const ibString& name, const ibValue& to) {
		const long prop = on.FindProp(name);
		ASSERT_NE(prop, wxNOT_FOUND) << name.ToStdString();
		on.SetPropVal(prop, to);
	}
	static ibValue Get(ibValue on, const ibString& name) {
		const long prop = on.FindProp(name);
		ibValue value;
		if (prop == wxNOT_FOUND)
			ADD_FAILURE() << "no property " << name.ToStdString();
		else
			on.GetPropVal(prop, value);
		return value;
	}

	ibValue RefOf(const ibValueMetaObjectRecordDataMutableRef* of, const ibString& id) const {
		return Call(Manager(of), wxT("GetRef"), { ibValue(id) });
	}

	// The rows the object's table holds under this identifier - read by the key, past any filter.
	long RowsUnder(const ibValueMetaObjectRecordDataMutableRef* of, const ibString& id) const {
		ibDataQueryBuilder rows;
		rows.From(of->GetQueryable()).WhereKey(ibGuid(id));
		rows.WithAccessPolicy(nullptr);
		ibDataQueryResult found = rows.Execute(ibReadPageRequest());
		long count = 0;
		while (found.Next())
			++count;
		return count;
	}

	// What the refusal said, or nothing when the call went through.
	template <typename Fn>
	static std::string Refusal(Fn&& call) {
		try {
			call();
		}
		catch (const ibBackendException& err) {
			return err.GetErrorDescription().ToStdString();
		}
		return std::string();
	}
};

} // namespace

TEST_F(CreateWithGivenRefFix, GetRef_Identifier_IsTheReferenceOfThisTypeWithIt)
{
	ibSessionScope bound(session.get());

	const ibValue ref = RefOf(products, kKnownId);
	ibValueReferenceDataObject* reference = nullptr;
	ASSERT_TRUE(ref.ConvertToValue(reference));
	ASSERT_NE(reference, nullptr);
	EXPECT_EQ(reference->GetMetaObjectRef(), products);
	EXPECT_FALSE(reference->IsEmptyRef());
	EXPECT_EQ(GuidOf(Call(ref, wxT("GetGuid"))), ibGuid(kKnownId));

	// The identifier as a Guid value reads the same as its text.
	const ibValue byGuid = Call(Manager(products), wxT("GetRef"), { ibValue(new ibValueGuid(ibGuid(kKnownId))) });
	EXPECT_TRUE(byGuid == ref);

	EXPECT_EQ(RowsUnder(products, kKnownId), 0) << "naming a reference writes nothing";
}

TEST_F(CreateWithGivenRefFix, GetRef_NilIdentifier_IsTheEmptyReference)
{
	ibSessionScope bound(session.get());

	const ibValue ref = Call(Manager(products), wxT("GetRef"), { ibValue(new ibValueGuid(ibGuid())) });
	EXPECT_TRUE(Call(ref, wxT("IsEmpty")).GetBoolean());
	EXPECT_TRUE(ref == Call(Manager(products), wxT("EmptyRef")));
}

TEST_F(CreateWithGivenRefFix, CreateItem_ReferenceGiven_WritesTheRowUnderIt)
{
	ibSessionScope bound(session.get());

	const ibValue ref = RefOf(products, kKnownId);
	ibValue item = Call(Manager(products), wxT("CreateItem"), { ref });
	ASSERT_FALSE(item.IsEmpty());
	EXPECT_TRUE(Call(item, wxT("IsNew")).GetBoolean()) << "it is a new object until written";
	EXPECT_TRUE(Call(Get(item, wxT("Ref")), wxT("IsEmpty")).GetBoolean()) << "a new object's Ref is empty, as ever";
	EXPECT_FALSE(Get(item, wxT("IsFolder")).GetBoolean());

	Set(item, wxT("Code"), ibValue(ibString(wxT("P-1"))));
	Set(item, wxT("Description"), ibValue(ibString(wxT("Bolt"))));
	std::string refused = Refusal([&] { Call(item, wxT("Write")); });
	ASSERT_EQ(refused, std::string());

	EXPECT_FALSE(Call(item, wxT("IsNew")).GetBoolean());
	EXPECT_TRUE(Get(item, wxT("Ref")) == ref) << "written, the object's Ref is the reference it was given";
	EXPECT_EQ(RowsUnder(products, kKnownId), 1);

	// ...and the reference now opens that object.
	ibValue opened = Call(ref, wxT("GetObject"));
	EXPECT_EQ(Get(opened, wxT("Description")).GetString(), ibString(wxT("Bolt")));

	// A second write is a rewrite of the same row, not another one.
	Set(item, wxT("Description"), ibValue(ibString(wxT("Bolt M6"))));
	refused = Refusal([&] { Call(item, wxT("Write")); });
	ASSERT_EQ(refused, std::string());
	EXPECT_EQ(RowsUnder(products, kKnownId), 1);
}

TEST_F(CreateWithGivenRefFix, CreateFolder_ReferenceGiven_WritesAFolderUnderIt)
{
	ibSessionScope bound(session.get());

	const ibValue ref = RefOf(products, kKnownId);
	ibValue folder = Call(Manager(products), wxT("CreateFolder"), { ref });
	ASSERT_FALSE(folder.IsEmpty());
	EXPECT_TRUE(Get(folder, wxT("IsFolder")).GetBoolean());
	Set(folder, wxT("Code"), ibValue(ibString(wxT("F-1"))));
	Set(folder, wxT("Description"), ibValue(ibString(wxT("Fasteners"))));
	const std::string refused = Refusal([&] { Call(folder, wxT("Write")); });
	ASSERT_EQ(refused, std::string());
	EXPECT_TRUE(Get(folder, wxT("Ref")) == ref);

	// An item written under ITS given reference names the folder by the folder's: no table of identities between.
	ibValue item = Call(Manager(products), wxT("CreateItem"), { RefOf(products, kOtherId) });
	Set(item, wxT("Code"), ibValue(ibString(wxT("P-1"))));
	Set(item, wxT("Description"), ibValue(ibString(wxT("Bolt"))));
	Set(item, wxT("Parent"), RefOf(products, kKnownId));
	const std::string itemRefused = Refusal([&] { Call(item, wxT("Write")); });
	ASSERT_EQ(itemRefused, std::string());
	ibValue opened = Call(RefOf(products, kOtherId), wxT("GetObject"));
	EXPECT_TRUE(Get(opened, wxT("Parent")) == ref);
}

TEST_F(CreateWithGivenRefFix, CreateDocument_ReferenceGiven_WritesTheRowUnderIt)
{
	ibSessionScope bound(session.get());

	const ibValue ref = RefOf(order, kKnownId);
	ibValue document = Call(Manager(order), wxT("CreateDocument"), { ref });
	ASSERT_FALSE(document.IsEmpty());
	EXPECT_TRUE(Call(document, wxT("IsNew")).GetBoolean());
	Set(document, wxT("Number"), ibValue(ibString(wxT("ORD-1"))));
	Set(document, wxT("Date"), ibValue(ibDateTime(2025, 7, 1, 10, 0, 0)));
	const std::string refused = Refusal([&] { Call(document, wxT("Write")); });
	ASSERT_EQ(refused, std::string());
	EXPECT_TRUE(Get(document, wxT("Ref")) == ref);
	EXPECT_EQ(RowsUnder(order, kKnownId), 1);
}

TEST_F(CreateWithGivenRefFix, CreateItem_NothingGiven_MintsAnIdentityOfItsOwn)
{
	ibSessionScope bound(session.get());

	ibValue first = Call(Manager(products), wxT("CreateItem"));
	ibValue second = Call(Manager(products), wxT("CreateItem"));
	for (ibValue* item : { &first, &second }) {
		ASSERT_FALSE(item->IsEmpty());
		Set(*item, wxT("Code"), ibValue(ibString(item == &first ? wxT("P-1") : wxT("P-2"))));
		Set(*item, wxT("Description"), ibValue(ibString(wxT("Bolt"))));
		const std::string refused = Refusal([&] { Call(*item, wxT("Write")); });
		ASSERT_EQ(refused, std::string());
	}
	EXPECT_FALSE(Call(Get(first, wxT("Ref")), wxT("IsEmpty")).GetBoolean());
	EXPECT_FALSE(Get(first, wxT("Ref")) == Get(second, wxT("Ref")));
}

TEST_F(CreateWithGivenRefFix, CreateItem_ReferenceTaken_IsRefusedNamingTheObject)
{
	ibSessionScope bound(session.get());

	ibValue item = Call(Manager(products), wxT("CreateItem"), { RefOf(products, kKnownId) });
	Set(item, wxT("Code"), ibValue(ibString(wxT("P-1"))));
	Set(item, wxT("Description"), ibValue(ibString(wxT("Bolt"))));
	ASSERT_EQ(Refusal([&] { Call(item, wxT("Write")); }), std::string());

	for (const wxChar* creator : { wxT("CreateItem"), wxT("CreateFolder") }) {
		const std::string refused = Refusal([&] { Call(Manager(products), creator, { RefOf(products, kKnownId) }); });
		EXPECT_NE(refused.find("Products"), std::string::npos) << refused;
		EXPECT_NE(refused.find("already exists"), std::string::npos) << refused;
		EXPECT_NE(refused.find(kKnownId.ToStdString()), std::string::npos) << refused;
	}
	EXPECT_EQ(RowsUnder(products, kKnownId), 1);
}

TEST_F(CreateWithGivenRefFix, CreateItem_ReferenceOfAnotherType_IsRefusedNamingTheObject)
{
	ibSessionScope bound(session.get());

	const std::string refused = Refusal([&] { Call(Manager(products), wxT("CreateItem"), { RefOf(units, kKnownId) }); });
	EXPECT_NE(refused.find("Products"), std::string::npos) << refused;
	EXPECT_NE(refused.find("of its own type"), std::string::npos) << refused;

	const std::string document = Refusal([&] { Call(Manager(order), wxT("CreateDocument"), { RefOf(products, kKnownId) }); });
	EXPECT_NE(document.find("Order"), std::string::npos) << document;
	EXPECT_NE(document.find("of its own type"), std::string::npos) << document;
}

TEST_F(CreateWithGivenRefFix, CreateItem_NotAReference_IsRefusedNamingTheObject)
{
	ibSessionScope bound(session.get());

	// An identifier is not a reference: GetRef is the road from one to the other.
	for (const ibValue& given : { ibValue(kKnownId), ibValue(new ibValueGuid(ibGuid(kKnownId))), ibValue(42) }) {
		const std::string refused = Refusal([&] { Call(Manager(products), wxT("CreateItem"), { given }); });
		EXPECT_NE(refused.find("Products"), std::string::npos) << refused;
		EXPECT_NE(refused.find("of its own type"), std::string::npos) << refused;
	}
}

TEST_F(CreateWithGivenRefFix, CreateItem_EmptyReference_IsRefusedNamingTheObject)
{
	ibSessionScope bound(session.get());

	const std::string refused = Refusal([&] {
		Call(Manager(products), wxT("CreateItem"), { Call(Manager(products), wxT("EmptyRef")) });
	});
	EXPECT_NE(refused.find("Products"), std::string::npos) << refused;
	EXPECT_NE(refused.find("empty reference"), std::string::npos) << refused;
}
