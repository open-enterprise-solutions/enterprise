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
// ibComposerNode::SetValue(unsigned int col, const ibVariant& variant, bool)
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
#include "backend/compiler/compileCode.h"   // ibCompileCode::GetCodeStyle — put back as it was found
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
#include "frmserver/visualView/ctrl/form.h"                     // a radio button on a form, typed by an enumeration
#include "frmserver/visualView/ctrl/formAttribute.h"
#include "backend/propertyManager/property/propertySource.h"
#include "backend/srcDataObject.h"
#include "backend/backend_mainFrame.h"           // a frame so the form can be born
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"   // MintUnlisted — a harness's session

using ibComposerNode = ibValueModel::ibComposerNode;

namespace {

constexpr unsigned int kCellCol = 777;   // arbitrary — ibComposerNode is keyed by raw ibMetaID, no real attribute needed

struct TabularEnumPersist : ::testing::Test {
	wxInitializer m_wxInit;
	std::shared_ptr<ibDatabaseLayerSQLite> db;
	ibString dbPath;
	std::unique_ptr<ibMetaDataConfigurationFile> cfg;
	ibValueMetaObjectEnumeration* vatRates = nullptr;
	ibValueMetaObjectEnum* vat10 = nullptr;
	ibValueMetaObjectEnum* vat20 = nullptr;
	// RunDatabase sets this thread's code style to the configuration's. The binary runs VES (test_compiler.cpp), so
	// TearDown puts it back, as the other fixtures that run a configuration do (AcceptanceCatalogs, FiberTrust).
	const short m_codeStyle = ibCompileCode::GetCodeStyle();

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

	static ibString SqliteTypeOf(const ibString& field) {
		if (field.EndsWith(wxT("_RRRef"))) return wxT(" BLOB");
		if (field.EndsWith(wxT("_D")) || field.EndsWith(wxT("_S"))) return wxT(" TEXT");
		if (field.EndsWith(wxT("_N"))) return wxT(" NUMERIC");
		return wxT(" INTEGER");
	}

	void CreateTable() {
		ibString list;
		std::set<ibString> seen;
		for (const ibValueMetaObjectAttributeBase* attribute : vatRates->GetGenericAttributeArrayObject()) {
			if (attribute == nullptr || attribute->GetQueryColumn() == nullptr)
				continue;
			for (const ibString& field : ColumnFieldNames(attribute->GetQueryColumn())) {
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
		const std::vector<ibString> fields = ColumnFieldNames(column);
		ibQueryStatement capture(ibQueryStatement::Kind::Delete, ibString(), fields);
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
// generic ibValue path. Before the fix this THREW ("Attempt to assign a value to
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
	ASSERT_NO_THROW(committed = node.SetValue(kCellCol, ibVariant(ibT("Vat10")), /*notify*/ false))
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


// A RADIO BUTTON ON A FORM OFFERS AN ENUMERATION'S MEMBERS TO CHOOSE AMONG — as the designer's inspector asks it, through
// the button's own property (ibPropertyValue::GetValueList). The members come from the form's configuration, which a
// control knows only through the const GetMetaData; asked through a non-const owner, the button had none and the
// drop-down was empty (an assert in a Debug build, 2026-11-05).
namespace {

// What hands the form its configuration, as the object a form edits does in the designer.
class ConfigurationSource : public ibSourceDataObject {
	const ibMetaData* m_metaData;
	ibSourceExplorer m_explorer;
public:
	explicit ConfigurationSource(const ibMetaData* metaData) : m_metaData(metaData) {}
	const ibSourceExplorer* GetSourceExplorer() const override { return &m_explorer; }
	void SourceIncrRef() override {}
	void SourceDecrRef() override {}
	bool IsEmpty() const override { return false; }
	const ibUniqueKey& GetGuid() const override { static const ibUniqueKey none; return none; }
	const ibValueMetaObjectGenericData* GetSourceMetaObject() const override { return nullptr; }
	const ibMetaData* GetSourceMetaData() const override { return m_metaData; }
	ibClassID GetSourceClassType() const override { return 0; }
	ibString GetSourceCaption() const override { return ibString(); }
};

// A form is born against its session's frame, as in test_runtime.cpp's FormRuntime.
struct EnumFormFrame : ibBackendDocFrame {
	explicit EnumFormFrame(ibSessionHolder&& holder) : ibBackendDocFrame(std::move(holder)) {}
	void SetTitle(const ibString&) override {}
	void SetStatusText(const ibString&, int) override {}
	void RefreshFrame() override {}
	void RaiseFrame() override {}
	ibUniqueKey CreateFormUniqueKey(const ibBackendControlFrame*, const ibSourceDataObject*,
		const ibUniqueKey& formGuid) override
	{
		if (formGuid.isValid())
			return formGuid;
		ibUniqueKey made = wxNewUniqueGuid;
		return made;
	}
};

struct EnumFormSession : ibSession {
	using ibSession::ibSession;
	ibBackendDocFrame* GetFrame() const override { return m_frame; }
	ibBackendDocFrame* m_frame = nullptr;
};

struct FormOverSource : ibValueForm {
	ConfigurationSource* m_source = nullptr;
	ibSourceDataObject* GetSourceObject() const override { return m_source; }
};

} // namespace

TEST_F(TabularEnumPersist, RadioButtonOffersTheMembersOfItsEnumeration) {
	ibValue member;
	ASSERT_TRUE(vatRates->ResolveQueryConstant(wxT("Vat10"), member));
	const ibClassID enumType = member.GetClassType();

	auto session = std::make_shared<EnumFormSession>(wxT("radio-enum"), ibSessionKind::Enterprise);
	EnumFormFrame frame(ibApplicationInstance::GetSessionRegistry()->MintUnlisted(session));
	session->m_frame = &frame;
	ibSessionScope scope(session.get());

	ConfigurationSource source(cfg.get());
	ibValuePtr<FormOverSource> form(new FormOverSource());
	form->m_source = &source;
	ibFormAttributeValue* const rate = form->AddAttribute(ibT("Rate"), enumType, ibValue());
	ASSERT_NE(rate, nullptr);

	ibValueFrame* const radio = form->NewObject(ibT("Radiobutton"), form);
	ASSERT_NE(radio, nullptr);
	ibPropertySource* const bound = dynamic_cast<ibPropertySource*>(radio->GetProperty(ibT("Source")));
	ASSERT_NE(bound, nullptr);
	bound->SetValue(ibSourceDescription(std::vector<ibSourceId>{ rate->GetId() }));

	ibPropertyChoiceList choices;
	EXPECT_NE(radio->GetProperty(ibT("ChoiceValue"))->GetValueList(choices), ibPropertyChoiceMode::None);
	ASSERT_EQ(choices.GetCount(), 2u);
	EXPECT_EQ(choices.GetName(0), ibString(ibT("Vat10")));
	EXPECT_EQ(choices.GetName(1), ibString(ibT("Vat20")));

	// Chosen by its name, as a caller writes one, the button stands for that member.
	ASSERT_TRUE(radio->GetProperty(ibT("ChoiceValue"))->SetNodeValue(ibDataValue::String(ibT("Vat20"))));
	ibValue chosen;
	radio->GetProperty(ibT("ChoiceValue"))->GetDataValue(chosen);
	EXPECT_EQ(chosen.GetClassType(), enumType);
	EXPECT_EQ(chosen.GetString(), wxT("Vat20"));
	form->m_source = nullptr;
}
