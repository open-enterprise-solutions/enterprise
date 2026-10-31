// =============================================================================
// A DOCUMENT JOURNAL - one list over the documents of several kinds - read live, on SQLite.
//
// Two documents in a configuration in memory, their tables made of exactly the fields the metaobject lays
// out, a few of each written, and a journal listing both. What it pins: the journal's rows are the
// documents' own, stacked on the server (one SELECT per document, UNION ALL), and every road reads that
// one source - Select() in code, `DocumentJournal.<Name>` in a query, the source the list form opens.
// =============================================================================

#include <gtest/gtest.h>

#include <set>
#include <vector>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/compiler/compileCode.h"
#include "backend/compiler/value.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/partial/document.h"
#include "backend/metaCollection/partial/documentJournal.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/objCtor.h"
#include "backend/composition/dataComposer.h"
#include "backend/query/columnLayout.h"
#include "core/query/queryParser.h"
#include "backend/query/queryLowering.h"
#include "backend/system/value/valueArray.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"
#include "frmserver/mcp/mcpTool.h"

namespace {

const ibDateTime kDay1(2025, 7, 1, 10, 0, 0);
const ibDateTime kDay2(2025, 7, 2, 10, 0, 0);
const ibDateTime kDay3(2025, 7, 3, 10, 0, 0);

struct DocumentJournalFix : ::testing::Test {
	wxInitializer                          m_wxInit;
	const short                            m_codeStyle = ibCompileCode::GetCodeStyle();
	std::shared_ptr<ibDatabaseLayerSQLite> db;

	std::unique_ptr<ibMetaDataConfigurationFile> cfg;
	ibValueMetaObjectDocument*        invoice = nullptr;
	ibValueMetaObjectDocument*        refund  = nullptr;
	ibValueMetaObjectDocumentJournal* journal = nullptr;

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
		invoice = dynamic_cast<ibValueMetaObjectDocument*>(cfg->CreateMetaObject(g_metaDocumentCLSID, root, false));
		refund  = dynamic_cast<ibValueMetaObjectDocument*>(cfg->CreateMetaObject(g_metaDocumentCLSID, root, false));
		journal = dynamic_cast<ibValueMetaObjectDocumentJournal*>(cfg->CreateMetaObject(g_metaDocumentJournalCLSID, root, false));
		ASSERT_NE(invoice, nullptr);
		ASSERT_NE(refund, nullptr);
		ASSERT_NE(journal, nullptr);
		invoice->SetName(wxT("Invoice"));
		refund->SetName(wxT("Refund"));
		journal->SetName(wxT("Sales"));

		// The two documents number differently - one by text, one by a number - which is what makes the
		// journal's Number a column of two kinds, read back by the tag beside it.
		refund->GetDocumentNumber()->GetTypeDesc().SetDefaultMetaType(ibValueTypes::TYPE_NUMBER);

		// What binding the documents in the designer or over MCP does, said directly.
		journal->GetRegisteredDocuments().AppendMetaType(invoice->GetMetaID());
		journal->GetRegisteredDocuments().AppendMetaType(refund->GetMetaID());

		ASSERT_TRUE(cfg->RunDatabase()) << "the configuration in memory did not run";

		for (const ibValueMetaObjectDocument* doc : { invoice, refund })
			db->RunQuery(wxT("%s"), wxT("CREATE TABLE ") + doc->GetQueryable()->GetQueryTableName()
				+ wxT(" (") + FieldList(doc) + wxT(")"));
	}
	void TearDown() override {
		if (cfg) cfg->CloseDatabase();
		cfg.reset();
		ibCompileCode::SetCodeStyle(m_codeStyle);
		if (ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}

	static std::vector<const ibBackendQueryColumn*> Columns(const ibValueMetaObjectDocument* doc) {
		return { doc->GetDataReference()->GetQueryColumn(), doc->GetDocumentDate()->GetQueryColumn(),
			doc->GetDocumentNumber()->GetQueryColumn(), doc->GetDocumentPosted()->GetQueryColumn(),
			doc->GetDataDeletionMark()->GetQueryColumn() };
	}
	// Declared by what each field holds, as the platform's own DDL declares them.
	static ibString FieldList(const ibValueMetaObjectDocument* doc) {
		ibString list;
		std::set<ibString> seen;
		for (const ibBackendQueryColumn* column : Columns(doc))
			for (const ibString& field : ColumnFieldNames(column))
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

	void Assign(std::vector<ibDmlAssign>& out, const ibBackendQueryColumn* column, const ibValue& value) const {
		const std::vector<ibString> fields = ColumnFieldNames(column);
		ibQueryStatement capture(ibQueryStatement::Kind::Delete, ibString(), fields);
		int position = 1;
		ibColumnCodec::WriteValue(column, cfg.get(), value, &capture, position);
		const std::vector<ibQueryExprPtr>& consts = capture.CapturedValues();
		for (size_t i = 0; i < fields.size(); ++i)
			out.push_back(ibDmlAssign{ fields[i], (i < consts.size() && consts[i]) ? consts[i] : ibConst(ibValue()) });
	}

	// A written document: its row, as the document's own write lays it out.
	ibValue Write(const ibValueMetaObjectDocument* doc, const ibDateTime& when, const ibValue& number) {
		const ibValue ref(ibValueReferenceDataObject::Create(cfg.get(), doc->GetMetaID(), ibGuid(ibGuid::newGuid())));
		std::vector<ibDmlAssign> row;
		Assign(row, doc->GetDataReference()->GetQueryColumn(), ref);
		Assign(row, doc->GetDocumentDate()->GetQueryColumn(), ibValue(when));
		Assign(row, doc->GetDocumentNumber()->GetQueryColumn(), number);
		Assign(row, doc->GetDocumentPosted()->GetQueryColumn(), ibValue(true));
		Assign(row, doc->GetDataDeletionMark()->GetQueryColumn(), ibValue(false));
		ibDatabaseQueryBuilder insert;
		EXPECT_GE(insert.Execute(ibInsert(doc->GetQueryable()->GetQueryTableName(), row)), 0);
		return ref;
	}

	static std::vector<ibValue> Refs(const ibValue& selected) {
		const ibValueArray* array = nullptr;
		return selected.ConvertToValue(array) && array != nullptr ? array->Values() : std::vector<ibValue>();
	}
	static bool Same(const ibValue& a, const ibValue& b) {
		return !a.IsEmpty() && !b.IsEmpty() && a.CompareValueLS(b) == 0 && b.CompareValueLS(a) == 0;
	}
};

} // namespace

// Only the configuration holds a journal, as only it holds documents.
TEST(DocumentJournal, TheConfigurationAcceptsOneAndACatalogDoesNot) {
	ibMetaDataConfigurationFile cfg;
	ibValueMetaObjectConfiguration* const root = cfg.GetCommonMetaObject();
	ASSERT_NE(root, nullptr);
	ibValueMetaObject* const journal = cfg.CreateMetaObject(g_metaDocumentJournalCLSID, root, false);
	ASSERT_NE(journal, nullptr);
	EXPECT_EQ(cfg.GetAnyArrayObject(g_metaDocumentJournalCLSID).size(), 1u);
	ibValueMetaObject* const catalog = cfg.CreateMetaObject(g_metaCatalogCLSID, root, false);
	ASSERT_NE(catalog, nullptr);
	EXPECT_EQ(cfg.CreateMetaObject(g_metaDocumentJournalCLSID, catalog, false), nullptr);
}

// The journal's columns are those every document has, each typed to hold every document's value: a reference
// to either document, a number kept as text by one and as a number by the other.
TEST_F(DocumentJournalFix, ItsColumnsHoldEveryDocumentsValue) {
	const ibDocumentJournalQueryable* rows = journal->GetQueryable();
	std::vector<ibString> names;
	for (const ibBackendQueryColumn* column : rows->GetColumns())
		names.push_back(column->GetName());
	EXPECT_EQ(names, (std::vector<ibString>{ wxT("Ref"), wxT("Date"), wxT("Number"), wxT("Posted"), wxT("DeletionMark") }));

	const ibTypeDescription& ref = rows->GetRefColumn()->GetTypeDesc();
	EXPECT_TRUE(ref.ContainType(reference_to_clsid(invoice->GetMetaID(), clsid_metaclass(invoice->GetClassType()))));
	EXPECT_TRUE(ref.ContainType(reference_to_clsid(refund->GetMetaID(), clsid_metaclass(refund->GetClassType()))));

	const ibTypeDescription& number = rows->ResolveColumnByName(wxT("number"))->GetTypeDesc();
	EXPECT_TRUE(number.ContainType(ibValueTypes::TYPE_STRING));
	EXPECT_TRUE(number.ContainType(ibValueTypes::TYPE_NUMBER));
}

// Select reads every listed document's rows, by date, whichever document each is.
TEST_F(DocumentJournalFix, SelectReadsBothDocumentsByDate) {
	const ibValue second = Write(invoice, kDay2, ibValue(ibT("INV-2")));
	const ibValue first  = Write(refund, kDay1, ibValue(ibNumber(7)));
	const ibValue third  = Write(invoice, kDay3, ibValue(ibT("INV-3")));

	const std::vector<ibValue> refs = Refs(journal->Select());
	ASSERT_EQ(refs.size(), 3u);
	EXPECT_TRUE(Same(refs[0], first));
	EXPECT_TRUE(Same(refs[1], second));
	EXPECT_TRUE(Same(refs[2], third));
	EXPECT_EQ(refs[0].GetClassType(), first.GetClassType()) << "a refund comes back a refund";
	EXPECT_EQ(refs[1].GetClassType(), second.GetClassType()) << "an invoice comes back an invoice";

	// Bounded, the dates are the date's: inclusive at both ends.
	const std::vector<ibValue> bounded = Refs(journal->Select(ibValue(kDay2), ibValue(kDay3)));
	ASSERT_EQ(bounded.size(), 2u);
	EXPECT_TRUE(Same(bounded[0], second));
	EXPECT_TRUE(Same(bounded[1], third));

	// …and the manager is what code reaches as `DocumentJournals.Sales`: the one its type registry vends.
	const ibCtorMetaValueType* managerType = journal->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Manager);
	ASSERT_NE(managerType, nullptr);
	ibValue manager = managerType->CreateObject(), selected;
	ASSERT_TRUE(manager.CallAsFunc(manager.FindMethod(wxT("Select")), selected, nullptr, 0));
	EXPECT_EQ(Refs(selected).size(), 3u);
}

// A query names the journal as it names a document - `DocumentJournal.Sales`, its columns by the documents'
// names - and the rows read through it are the same, the number of each in the kind its document keeps.
// Read the way a list reads them: the composer over the journal's source.
TEST_F(DocumentJournalFix, AQueryReadsTheSameRows) {
	Write(invoice, kDay2, ibValue(ibT("INV-2")));
	Write(refund, kDay1, ibValue(ibNumber(7)));

	ibQueryParser parser;
	const ibQueryPackage package = parser.ParsePackage(
		wxT("SELECT Ref, Number FROM DocumentJournal.Sales WHERE Posted ORDER BY Date"));
	ASSERT_NE(package.SingleSelect(), nullptr);
	const ibSourceMetaDataScope scope(cfg.get());
	EXPECT_NO_THROW(ibQueryLowering::CheckNames(package, {}));
	std::vector<ibQueryLowering::OutputColumn> described;
	ASSERT_NO_THROW(ibQueryLowering::DescribeOutput(*package.SingleSelect(), {}, described));
	ASSERT_EQ(described.size(), 2u);

	const ibDocumentJournalQueryable* rows = journal->GetQueryable();
	const ibBackendQueryColumn* number = rows->ResolveColumnByName(wxT("Number"));
	ibDataDBComposer composer;
	composer.FromSource(rows);
	composer.Select(rows->GetRefColumn());
	composer.Select(number);
	std::vector<ibQueryLowering::OutputColumn> schema;
	bool hasTotals = false;
	ibDataQueryResult result = composer.Execute(schema, hasTotals);

	std::map<ibString, ibValue> numbers;   // by the kind of the document the row is
	while (result.Next())
		numbers[result.GetValue(rows->GetRefColumn()).GetClassName()] = result.GetValue(number);
	ASSERT_EQ(numbers.size(), 2u);
	for (const auto& [kind, value] : numbers) {
		if (value.GetType() == ibValueTypes::TYPE_NUMBER)
			EXPECT_EQ(value.GetNumber(), ibNumber(7)) << kind.ToStdString();
		else
			EXPECT_EQ(value.GetString(), ibString(ibT("INV-2"))) << kind.ToStdString();
	}
}

// The list form opens over the same source, and a row of it is the document: its value is the reference and
// so is its identity, whole - a guid alone would not say which document's.
TEST_F(DocumentJournalFix, TheListIsTheSameRowsAndARowIsItsDocument) {
	EXPECT_EQ(journal->GetFormType().GetItemCount(), 1u);

	const ibQueryableSourceDescriptor* source = cfg->GetSourceFactory()->FindDescriptor(wxT("DocumentJournal"), wxT("Sales"));
	ASSERT_NE(source, nullptr) << "the journal is a source by its kind and name";

	const ibValue doc = Write(refund, kDay1, ibValue(ibNumber(7)));
	ibRowMetaValues row;
	row[journal->GetQueryable()->GetRefColumn()->GetColumnId()] = doc;
	EXPECT_TRUE(Same(source->GetSelectValue(row), doc));
	EXPECT_TRUE(Same(source->GetItemKey(row).GetKey(journal->GetQueryable()->GetRefColumn()->GetColumnId()), doc));
}

// A journal that lists nothing has no rows - and still the columns, answered in memory rather than as a
// statement over no table.
TEST_F(DocumentJournalFix, AJournalOfNothingHasNoRows) {
	journal->GetRegisteredDocuments().ClearMetaType();
	EXPECT_TRUE(journal->GetQueryable()->IsComputedInRam());
	EXPECT_EQ(journal->GetQueryable()->GetColumns().size(), 5u);
	EXPECT_TRUE(Refs(journal->Select()).empty());
}

// ⭐ OVER MCP, AS AN ASSISTANT BUILDS ONE: `metadata_create` makes the journal and the documents, `metadata_bind`
// lists them in RegisteredDocuments, and the journal reads them - the same binding the designer's property grid
// sets. The configuration is the one a designer session edits (test_commonModuleCheck.cpp says why each step is
// there).
TEST(DocumentJournalOverMcp, IsCreatedAndBoundByTheTools) {
	wxInitializer wx;
	if (!wx.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";
	if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
		GTEST_SKIP() << "appData env unavailable headless";
	struct Release {
		short m_codeStyle = ibCompileCode::GetCodeStyle();
		~Release() {
			ibCompileCode::SetCodeStyle(m_codeStyle);
			ibApplicationInstance::DestroyAppDataEnv();
		}
	} release;

	ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
	ASSERT_NE(pool, nullptr);
	std::shared_ptr<ibDatabaseLayerSQLite> db = std::make_shared<ibDatabaseLayerSQLite>();
	ASSERT_TRUE(db->Open(wxT(":memory:")));
	pool->Init(db, /*maxSize=*/1, /*minIdle=*/0);
	ibMetaDataConfigurationStorage::CreateConfigTable();
	ibMetaDataConfigurationStorage::CreateConfigSaveTable();
	ibMetaDataConfigurationStorage::CreateConfigSequence();

	ibSessionRegistry* const registry = ibApplicationInstance::GetSessionRegistry();
	ASSERT_NE(registry, nullptr);
	ibSessionHolder holder = registry->MintUnlisted(
		std::make_shared<ibSession>(wxT("journal-over-mcp"), ibSessionKind::Designer));
	ibSession* const session = holder.Get();
	ASSERT_NE(session, nullptr);
	ibSessionScope scope(session);
	registry->NotifyAuthenticated(session);
	struct Leave {
		ibSessionRegistry* m_registry;
		ibSession* m_session;
		~Leave() { m_registry->NotifyDisconnect(m_session); }
	} leave{ registry, session };

	ibMetaDataConfigurationBase* cfg = session->GetMetaData();
	ASSERT_NE(cfg, nullptr);
	if (!cfg->IsConfigOpen())
		ASSERT_TRUE(cfg->RunDatabase());

	const auto call = [](const ibString& tool, const std::vector<std::pair<ibString, ibString>>& args, ibMetaID id = 0) {
		const ibMcpTool* found = ibFindMcpTool(tool);
		EXPECT_NE(found, nullptr) << tool.ToStdString();
		ibDataNode params, result;
		if (id != 0)
			params.AddField(ibT("id"), ibDataValue::Int(static_cast<std::int64_t>(id)));
		for (const auto& arg : args)
			params.SetValue(arg.first, arg.second);
		ibString refusal;
		const bool done = found != nullptr && found->Call(params, result, refusal);
		EXPECT_TRUE(done) << tool.ToStdString() << ": " << refusal.ToStdString();
		return done;
	};

	ASSERT_TRUE(call(ibT("metadata_create"), { { ibT("kind"), ibT("Document") }, { ibT("name"), ibT("Invoice") } }));
	ASSERT_TRUE(call(ibT("metadata_create"), { { ibT("kind"), ibT("Document") }, { ibT("name"), ibT("Refund") } }));
	ASSERT_TRUE(call(ibT("metadata_create"), { { ibT("kind"), ibT("DocumentJournal") }, { ibT("name"), ibT("Sales") } }));

	ibValueMetaObjectDocumentJournal* journal = nullptr;
	for (ibValueMetaObject* object : cfg->GetAnyArrayObject(g_metaDocumentJournalCLSID))
		if (object->GetName() == ibT("Sales"))
			journal = dynamic_cast<ibValueMetaObjectDocumentJournal*>(object);
	ASSERT_NE(journal, nullptr);

	ASSERT_TRUE(call(ibT("metadata_bind"), { { ibT("property"), ibT("RegisteredDocuments") }, { ibT("target"), ibT("Invoice") } }, journal->GetMetaID()));
	ASSERT_TRUE(call(ibT("metadata_bind"), { { ibT("property"), ibT("RegisteredDocuments") }, { ibT("target"), ibT("Document.Refund") } }, journal->GetMetaID()));

	std::vector<ibString> listed;
	for (const ibValueMetaObjectDocument* doc : journal->GetDocuments())
		listed.push_back(doc->GetName());
	EXPECT_EQ(listed, (std::vector<ibString>{ ibT("Invoice"), ibT("Refund") }));
	EXPECT_EQ(journal->GetQueryable()->GetRefColumn()->GetTypeDesc().GetClsidCount(), 2u) << "a reference to either document";
}
