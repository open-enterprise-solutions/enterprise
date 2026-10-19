// =============================================================================
// OES Enterprise — issue #482: an EnumRef value in a tabular-section cell is
// not persisted, while the same EnumRef type on a header attribute round-trips
// normally.
//
// The backend write path (SaveData -> ibDataQueryBuilder::SetValue ->
// ExecuteWrite -> BindValue/DriveSpread) is IDENTICAL for a header attribute
// and a tabular-section attribute — same functions, same codec, confirmed by
// reading tabularSectionQuery.cpp / commonObjectRefQuery.cpp / dbTableProvider.cpp
// side by side. Schema contribution (DDL columns) is likewise identical
// (commonObjectSchema.cpp: `t.Add(object->GetQueryColumn())` for both).
//
// The actual defect is in how a GRID commits an edited cell:
// ibComposerNode::SetValue(unsigned int col, const wxVariant& variant, bool)
// (tabularModel.h) resolves the editor's committed text to a real value via
// FindValue, then called `cValue.SetValue(cFoundedValue)` to store it. For a
// reference-kind cell (TYPE_REFFER — any EnumRef/CatalogRef/DocumentRef),
// ibValue::SetValue(const ibValue&) FORWARDS into the referenced object
// (`m_pRef->SetValue(...)`) rather than repointing the wrapper — and that
// object is unconditionally constructed read-only
// (ibValueReferenceDataObject : ibValueDynamicMembers(TYPE_VALUE, /*readOnly*/ true)),
// so the write raised "Attempt to assign a value to a write-denied variable"
// on every reference/enum pick made through a tabular grid, and the edit
// never reached the row. A header attribute's form control never goes
// through this path — it builds the chosen value and calls
// SetValueByMetaID(id, value) directly, which is why the identical type
// worked there. Fixed by assigning (`cValue = cFoundedValue`), which
// repoints the wrapper instead of mutating the read-only referenced object.
//
// This test exercises exactly that grid-commit path against a real,
// programmatically-built Enumeration (no Document/TabularSection metaobject
// needed — ibComposerNode is model-free, keyed by a raw column id).
// =============================================================================

#include <gtest/gtest.h>

#include <wx/filename.h>
#include <wx/init.h>

#include "backend/appData.h"
#include "backend/tabularModel.h"   // ibValueModel::ibComposerNode
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/partial/enumeration.h"
#include "backend/metaCollection/enumeration/metaEnumObject.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/query/dataQueryBuilder.h"
#include "backend/query/columnLayout.h"
#include "core/clsid.h"

using ibComposerNode = ibValueModel::ibComposerNode;

namespace {

constexpr unsigned int kCellCol = 777;   // arbitrary — ibComposerNode is keyed by raw ibMetaID, no real attribute needed

struct TabularEnumPersist : ::testing::Test {
	wxInitializer m_wxInit;
	std::shared_ptr<ibDatabaseLayerSQLite> db;
	wxString dbPath;
	std::unique_ptr<ibMetaDataConfigurationFile> cfg;
	ibValueMetaObjectEnumeration* vatRates = nullptr;
	ibValueMetaObjectEnum* vat10 = nullptr;
	ibValueMetaObjectEnum* vat20 = nullptr;

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";
		if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
			GTEST_SKIP() << "appData env unavailable headless";
		ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
		if (pool == nullptr)
			GTEST_SKIP() << "no connection pool after CreateAppDataEnv";
		db = std::make_shared<ibDatabaseLayerSQLite>();
		dbPath = wxFileName::CreateTempFileName(wxT("oesenum"));
		if (!db->Open(dbPath))
			GTEST_SKIP() << "SQLite open failed";
		pool->Init(db, /*maxSize=*/4, /*minIdle=*/0);

		cfg = std::make_unique<ibMetaDataConfigurationFile>();
		ibValueMetaObjectConfiguration* root = cfg->GetCommonMetaObject();
		ASSERT_NE(root, nullptr);

		vatRates = dynamic_cast<ibValueMetaObjectEnumeration*>(
			cfg->CreateMetaObject(g_metaEnumerationCLSID, root, /*runObject*/ false));
		ASSERT_NE(vatRates, nullptr);
		vatRates->SetName(wxT("VatRates"));

		vat10 = dynamic_cast<ibValueMetaObjectEnum*>(
			cfg->CreateMetaObject(g_metaEnumCLSID, vatRates, false));
		vat20 = dynamic_cast<ibValueMetaObjectEnum*>(
			cfg->CreateMetaObject(g_metaEnumCLSID, vatRates, false));
		ASSERT_NE(vat10, nullptr);
		ASSERT_NE(vat20, nullptr);
		vat10->SetName(wxT("Vat10"));
		vat20->SetName(wxT("Vat20"));

		ASSERT_TRUE(cfg->RunDatabase()) << "the configuration in memory did not run";

		CreateTable();
		SeedRows();
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

	static wxString SqliteTypeOf(const wxString& field) {
		if (field.EndsWith(wxT("_RRRef"))) return wxT(" BLOB");
		if (field.EndsWith(wxT("_D")) || field.EndsWith(wxT("_S"))) return wxT(" TEXT");
		if (field.EndsWith(wxT("_N"))) return wxT(" NUMERIC");
		return wxT(" INTEGER");
	}

	void CreateTable() {
		wxString list;
		std::set<wxString> seen;
		for (const ibValueMetaObjectAttributeBase* attribute : vatRates->GetGenericAttributeArrayObject()) {
			if (attribute == nullptr || attribute->GetQueryColumn() == nullptr)
				continue;
			for (const wxString& field : ColumnFieldNames(attribute->GetQueryColumn())) {
				if (!seen.insert(field).second)
					continue;
				list << (list.IsEmpty() ? wxT("") : wxT(", ")) << field << SqliteTypeOf(field);
			}
		}
		ASSERT_FALSE(list.IsEmpty());
		db->RunQuery(wxT("%s"), wxT("CREATE TABLE ") + vatRates->GetQueryable()->GetQueryTableName()
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

	void SeedRows() {
		int position = 0;
		for (ibValueMetaObjectEnum* member : vatRates->GetEnumObjectArray()) {
			std::vector<ibDmlAssign> row;
			const ibValue ref(ibValueReferenceDataObject::Create(vatRates, member->GetGuid(), ibReferenceLoad::OnDemand));
			Assign(row, vatRates->GetDataReference()->GetQueryColumn(), ref);
			Assign(row, vatRates->GetDataOrder()->GetQueryColumn(), ibValue(position++));
			ibDatabaseQueryBuilder insert;
			ASSERT_GE(insert.Execute(ibInsert(vatRates->GetQueryable()->GetQueryTableName(), row)), 0);
		}
	}
};

} // namespace

// Sanity: the backend can resolve a member by name directly (no grid involved) —
// this is the "header-correct" path and MUST pass, or the fixture itself is broken.
TEST_F(TabularEnumPersist, ResolveQueryConstant_FindsTheMemberDirectly) {
	ibValue resolved;
	ASSERT_TRUE(vatRates->ResolveQueryConstant(wxT("Vat10"), resolved));
	EXPECT_FALSE(resolved.IsEmpty());
}

// The crux: an EMPTY, PROPERLY-TYPED cell (what a freshly-added tabular row starts
// with, same as object->CreateValue() would hand it) committed through the GRID's
// generic wxVariant path. Before the fix this THREW ("Attempt to assign a value to
// a write-denied variable") instead of silently no-opping, because the cell's
// referenced object is read-only by construction and the old code tried to mutate
// it in place rather than repoint the wrapper — see the file header.
TEST_F(TabularEnumPersist, GridCommit_ActuallySetsTheChosenMember) {
	ibComposerNode node;
	const ibValue empty(ibValueReferenceDataObject::Create(vatRates));   // empty, typed — what a new row cell starts as
	node.AppendTableValue(kCellCol) = empty;

	ASSERT_TRUE(node.GetTableValue(kCellCol).IsEmpty()) << "fixture sanity: cell starts empty";

	// What the dataview grid calls when a person commits an edited cell.
	bool committed = false;
	ASSERT_NO_THROW(committed = node.SetValue(kCellCol, wxVariant(wxT("Vat10")), /*notify*/ false))
		<< "committing a reference/enum pick in a tabular grid must not throw (#482)";
	EXPECT_TRUE(committed);

	const ibValue& afterEdit = node.GetTableValue(kCellCol);
	ASSERT_FALSE(afterEdit.IsEmpty())
		<< "the cell is still empty after committing \"Vat10\" — issue #482's persistence symptom, "
		   "reproduced at the ibComposerNode level.";
	EXPECT_EQ(afterEdit.GetString(), wxT("Vat10"));
}

// Narrower still: isolate FindValue itself, bypassing SetValue's own bookkeeping. This half of the
// mechanism was never broken — FindValue correctly resolves an empty-but-typed EnumRef cell by name;
// the defect was entirely in what SetValue did with the match it got back.
TEST_F(TabularEnumPersist, FindValue_OnAnEmptyTypedEnumCell_FindsTheMemberByName) {
	const ibValue empty(ibValueReferenceDataObject::Create(vatRates));
	std::vector<ibValue> candidates;
	const bool found = empty.FindValue(wxT("Vat10"), candidates);
	ASSERT_TRUE(found) << "ibValue::FindValue found nothing for an empty-but-typed EnumRef cell";
	ASSERT_FALSE(candidates.empty());
	EXPECT_EQ(candidates.front().GetString(), wxT("Vat10"));
}

