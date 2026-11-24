// =============================================================================
// A DOCUMENT JOURNAL - one list over the documents of several kinds - read live, on SQLite.
//
// Two documents in an application in memory, their tables made of exactly the fields the metaobject lays
// out, a few of each written, and a journal listing both. What it pins: the journal's rows are the
// documents' own, stacked on the server (one SELECT per document, UNION ALL), and every road reads that
// one source - the selection in code, `DocumentJournal.<Name>` in a query, the source the list form opens.
//
// ⭐ AND A BRANCH IS READ AS ITS DOCUMENT IS. Under a user in a role, a document the role may not read is
// refused through the journal, by name, and a document's row filter keeps its rows out of the journal too.
// =============================================================================

#include <gtest/gtest.h>
#include <wx/string.h>   // wxT, wxString - the engine's headers include no wx

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/compiler/compileCode.h"
#include "backend/compiler/value.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metadataApplication.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/metaRoleObject.h"
#include "backend/metaCollection/partial/document.h"
#include "backend/metaCollection/partial/documentJournal.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/objCtor.h"
#include "backend/composition/dataComposer.h"
#include "backend/query/columnLayout.h"
#include "backend/query/dataQueryBuilder.h"
#include "backend/query/provider/database/columnCodec.h"
#include "backend/query/queryableFactory.h"
#include "backend/query/queryLowering.h"
#include "backend/query/tempTableQueryable.h"   // ibCteQueryable — a declared query, read through
#include "backend/restructureInfo.h"   // the save's ledger — what a refused save says
#include "core/picturePredefined.h"    // g_picRowDocument* — a row's picture
#include "core/query/queryParser.h"
#include "backend/metaCollection/partial/selector/objectSelector.h"   // the journal's selection — Select() in code
#include "backend/system/value/valueType.h"   // ibValueType — a row's document, as a Type
#include "backend/system/value/valueQuery.h"  // ibValueQueryExec — a query's text, run as a script runs it
#include "backend/system/value/valueTable.h"  // ibValueModelTable — its rows, unloaded
#include "backend/session/session.h"
#include "backend/session/sessionHolder.h"
#include "backend/session/sessionRegistry.h"
#include "backend/userInfo.h"
#include "frmserver/mcp/mcpTool.h"

namespace {

const ibDateTime kDay1(2025, 7, 1, 10, 0, 0);
const ibDateTime kDay2(2025, 7, 2, 10, 0, 0);
const ibDateTime kDay3(2025, 7, 3, 10, 0, 0);

// The role's row filter: of the invoices, the one numbered INV-1; every other source as it is.
const ibString kOneInvoice =
	wxT("Procedure OnAccessRead(Source, Operation, Allowed) {\n")
	wxT("\tif (Source == \"Document.Invoice\") {\n")
	wxT("\t\tSource = Source.Where(Function(x) { return x.Number == \"INV-1\"; });\n")
	wxT("\t}\n")
	wxT("\tAllowed = true;\n")
	wxT("}\n");

struct DocumentJournalFix : ::testing::Test {
	wxInitializer                          m_wxInit;
	// RunDatabase sets this thread's code style to the application's. The binary runs VES
	// (test_compiler.cpp), so TearDown puts it back, as StyleGuard does.
	const short                            m_codeStyle = ibCompileCode::GetCodeStyle();
	std::shared_ptr<ibDatabaseLayerSQLite> db;

	std::unique_ptr<ibMetaDataApplicationFile> cfg;
	ibValueMetaObjectDocument*        invoice = nullptr;
	ibValueMetaObjectDocument*        refund  = nullptr;
	ibValueMetaObjectDocumentJournal* journal = nullptr;
	ibValueMetaObjectDocumentJournal* nothing = nullptr;   // a journal that lists no document
	ibValueMetaObjectRole*            role    = nullptr;   // governs only a session a user in it reads under

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

		cfg = std::make_unique<ibMetaDataApplicationFile>();
		ibValueMetaObjectApplication* root = cfg->GetRootMetaObject();
		ASSERT_NE(root, nullptr);
		invoice = dynamic_cast<ibValueMetaObjectDocument*>(cfg->CreateMetaObject(g_metaDocumentCLSID, root, false));
		refund  = dynamic_cast<ibValueMetaObjectDocument*>(cfg->CreateMetaObject(g_metaDocumentCLSID, root, false));
		journal = dynamic_cast<ibValueMetaObjectDocumentJournal*>(cfg->CreateMetaObject(g_metaDocumentJournalCLSID, root, false));
		nothing = dynamic_cast<ibValueMetaObjectDocumentJournal*>(cfg->CreateMetaObject(g_metaDocumentJournalCLSID, root, false));
		role    = dynamic_cast<ibValueMetaObjectRole*>(cfg->CreateMetaObject(g_metaRoleCLSID, root, false));
		ASSERT_NE(invoice, nullptr);
		ASSERT_NE(refund, nullptr);
		ASSERT_NE(journal, nullptr);
		ASSERT_NE(nothing, nullptr);
		ASSERT_NE(role, nullptr);
		invoice->SetName(wxT("Invoice"));
		refund->SetName(wxT("Refund"));
		journal->SetName(wxT("Sales"));
		nothing->SetName(wxT("Nothing"));
		role->SetName(wxT("Clerk"));
		// A role with administration grants everything and filters nothing.
		ASSERT_TRUE(root->SetRight(wxT("Administration"), role->GetMetaID(), false));

		// The two documents number differently - one by text, one by a number - which is what makes the
		// journal's Number a column of two kinds, read back by the tag beside it.
		refund->GetDocumentNumber()->GetTypeDesc().SetDefaultMetaType(ibValueTypes::TYPE_NUMBER);

		// What binding the documents in the designer or over MCP does, said directly.
		journal->GetRegisteredDocuments().AppendMetaType(invoice->GetMetaID());
		journal->GetRegisteredDocuments().AppendMetaType(refund->GetMetaID());

		// ⭐ RUN IT. A reference read back from a row is created through the application's TYPE registry, and the
		// journal's reading is typed by the documents it lists when it runs.
		ASSERT_TRUE(cfg->RunDatabase()) << "the application in memory did not run";

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

	static std::shared_ptr<const ibDocumentJournalQueryable> Rows(const ibValueMetaObjectDocumentJournal* of) {
		return std::dynamic_pointer_cast<const ibDocumentJournalQueryable>(of->GetQueryable());
	}

	static std::vector<std::shared_ptr<const ibBackendQueryColumn>> Columns(const ibValueMetaObjectDocument* doc) {
		return { doc->GetDataReference()->GetQueryColumn(), doc->GetDocumentDate()->GetQueryColumn(),
			doc->GetDocumentNumber()->GetQueryColumn(), doc->GetDocumentPosted()->GetQueryColumn(),
			doc->GetDataDeletionMark()->GetQueryColumn() };
	}
	// Declared by what each field holds, as the platform's own DDL declares them.
	static ibString FieldList(const ibValueMetaObjectDocument* doc) {
		ibString list;
		std::set<ibString> seen;
		for (const std::shared_ptr<const ibBackendQueryColumn>& column : Columns(doc))
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

	// A value said as the fields that hold it - the same codec a document's write goes through.
	void Assign(std::vector<ibDmlAssign>& out, const std::shared_ptr<const ibBackendQueryColumn>& column, const ibValue& value) const {
		const std::vector<ibString> fields = ColumnFieldNames(*column);
		ibQueryStatement capture(ibQueryStatement::Kind::Delete, ibString(), fields);
		int position = 1;
		ibColumnCodec::WriteValue(*column, cfg.get(), value, &capture, position);
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

	// The rows a query's text gives, run as a script runs it - New Query(text).Execute().Unload(); -1 when it gives none.
	long RowsOf(const ibString& text) const {
		// A query resolves its sources in the application it runs on behalf of.
		const ibSourceMetaDataScope application(cfg.get());
		ibValuePtr<ibValueQueryExec> query(new ibValueQueryExec());
		ibValue said(text);
		ibValue* init[] = { &said };
		EXPECT_TRUE(query->Init(init, 1)) << text;
		ibValue result;
		EXPECT_TRUE(query->CallAsFunc(query->FindMethod(wxT("Execute")), result, nullptr, 0)) << text;
		ibValueQueryResult* rows = nullptr;
		ibValue unloaded;
		ibValueModelTable* table = nullptr;
		if (!result.ConvertToValue(rows) || rows == nullptr
		    || !rows->CallAsFunc(rows->FindMethod(wxT("Unload")), unloaded, nullptr, 0)
		    || !unloaded.ConvertToValue(table) || table == nullptr)
			return -1;
		return table->GetRowCount();
	}

	// A session of a user in the role, its runtime compiled: what builds the policy every read under it carries.
	std::shared_ptr<ibSession> SessionInRole(const ibString& roleModule) {
		const_cast<ibValueMetaObjectManagerModule*>(role->GetRoleModule())->SetModuleText(roleModule);

		auto session = std::make_shared<ibSession>(wxT("journal-clerk"), ibSessionKind::Enterprise);
		ibUserInfo user;
		user.m_strUserGuid = wxT("22222222-2222-2222-2222-222222222222");
		user.m_strUserName = wxT("clerk");
		ibUserInfo::ibUserRole membership;
		membership.m_miRoleId = role->GetMetaID();
		membership.m_strRoleName = wxT("Clerk");
		user.m_roleArray.push_back(membership);
		ibSessionRegistry* registry = ibApplicationInstance::GetSessionRegistry();
		if (registry == nullptr)
			return nullptr;
		registry->InstallUser(session.get(), user, wxEmptyString);

		ibSessionScope bound(session.get());
		if (session->CreateRoot(cfg.get()) == nullptr)
			return nullptr;
		try {
			if (!session->CompileRoot())
				return nullptr;
		}
		catch (const ibBackendException& err) {
			ADD_FAILURE() << err.GetErrorDescription().ToStdString();
			return nullptr;
		}
		return session;
	}
	// …and its runtime let go before the application closes.
	struct RootCleared {
		ibSession* m_session;
		~RootCleared() { if (m_session != nullptr) m_session->ClearRoot(); }
	};

	// The journal's selection - what `DocumentJournals.<Name>.Select(From, To)` hands code.
	static ibValue Selection(const ibValueMetaObjectDocumentJournal* of, const ibValue& from = ibValue(), const ibValue& to = ibValue()) {
		return ibValue(new ibValueSelectorDocumentJournalDataObject(of, from, to));
	}
	// …walked to its end as code walks it - Next(), and the row's Ref.
	static std::vector<ibValue> Refs(const ibValue& selected) {
		std::vector<ibValue> refs;
		ibValueSelectorDocumentJournalDataObject* selection = nullptr;
		if (!selected.ConvertToValue(selection) || selection == nullptr)
			return refs;
		const long ref = selection->FindProp(wxT("Ref"));
		while (selection->Next()) {
			ibValue one;
			selection->GetPropVal(ref, one);
			refs.push_back(one);
		}
		return refs;
	}
	static bool Same(const ibValue& a, const ibValue& b) {
		return !a.IsEmpty() && !b.IsEmpty() && a.CompareValueLS(b) == 0 && b.CompareValueLS(a) == 0;
	}
};

} // namespace

// Only the application holds a journal, as only it holds documents.
TEST(DocumentJournal, TheApplicationAcceptsOneAndACatalogDoesNot) {
	ibMetaDataApplicationFile cfg;
	ibValueMetaObjectApplication* const root = cfg.GetRootMetaObject();
	ASSERT_NE(root, nullptr);
	ibValueMetaObject* const journal = cfg.CreateMetaObject(g_metaDocumentJournalCLSID, root, false);
	ASSERT_NE(journal, nullptr);
	EXPECT_EQ(cfg.GetAnyArrayObject(g_metaDocumentJournalCLSID).size(), 1u);
	ibValueMetaObject* const catalog = cfg.CreateMetaObject(g_metaCatalogCLSID, root, false);
	ASSERT_NE(catalog, nullptr);
	EXPECT_EQ(cfg.CreateMetaObject(g_metaDocumentJournalCLSID, catalog, false), nullptr);
}

// The journal is read under a right of its own, so the role editor lists it as it lists every source a person opens.
TEST(DocumentJournal, HasAReadRightOfItsOwn) {
	ibMetaDataApplicationFile cfg;
	ibValueMetaObject* const journal = cfg.CreateMetaObject(g_metaDocumentJournalCLSID, cfg.GetRootMetaObject(), false);
	ASSERT_NE(journal, nullptr);
	EXPECT_EQ(journal->GetRoleCount(), 1u);
	EXPECT_NE(journal->GetRole(wxT("Read")), nullptr);
}

// The journal's columns are those every document has, each typed to hold every document's value: a reference
// to either document, a number kept as text by one and as a number by the other.
TEST_F(DocumentJournalFix, ItsColumnsHoldEveryDocumentsValue) {
	const std::shared_ptr<const ibDocumentJournalQueryable> rows = Rows(journal);
	ASSERT_NE(rows, nullptr);
	std::vector<ibString> names;
	for (const std::shared_ptr<const ibBackendQueryColumn>& column : rows->GetQueryColumns())
		names.push_back(column->GetName());
	EXPECT_EQ(names, (std::vector<ibString>{ wxT("Ref"), wxT("Date"), wxT("Number"), wxT("DocumentType"), wxT("Posted"), wxT("DeletionMark") }));

	const ibTypeDescription& ref = rows->GetRefColumn()->GetTypeDesc();
	EXPECT_TRUE(ref.ContainType(reference_to_clsid(invoice->GetMetaID(), clsid_metaclass(invoice->GetClassType()))));
	EXPECT_TRUE(ref.ContainType(reference_to_clsid(refund->GetMetaID(), clsid_metaclass(refund->GetClassType()))));

	const ibTypeDescription& number = rows->ResolveColumnByName(wxT("number"))->GetTypeDesc();
	EXPECT_TRUE(number.ContainType(ibValueTypes::TYPE_STRING));
	EXPECT_TRUE(number.ContainType(ibValueTypes::TYPE_NUMBER));
}

// The selection walks every listed document's rows, whichever document each is, over a period or all of them: a
// row's reference is its own document's, and its object that document's object.
TEST_F(DocumentJournalFix, SelectionWalksBothDocuments) {
	const ibValue invoiced = Write(invoice, kDay2, ibValue(ibT("INV-2")));
	const ibValue refunded = Write(refund, kDay1, ibValue(ibNumber(7)));
	const ibValue another  = Write(invoice, kDay3, ibValue(ibT("INV-3")));

	const std::vector<ibValue> refs = Refs(Selection(journal));
	ASSERT_EQ(refs.size(), 3u);
	for (const ibValue& written : { invoiced, refunded, another }) {
		const auto found = std::find_if(refs.begin(), refs.end(), [&written](const ibValue& ref) { return Same(ref, written); });
		ASSERT_NE(found, refs.end());
		EXPECT_EQ(found->GetClassType(), written.GetClassType()) << "a refund comes back a refund, an invoice an invoice";
	}

	// Over a period, the dates are the date's: inclusive at both ends.
	const std::vector<ibValue> bounded = Refs(Selection(journal, ibValue(kDay2), ibValue(kDay3)));
	ASSERT_EQ(bounded.size(), 2u);
	for (const ibValue& written : { invoiced, another })
		EXPECT_NE(std::find_if(bounded.begin(), bounded.end(), [&written](const ibValue& ref) { return Same(ref, written); }), bounded.end());

	const ibValuePtr<ibValueSelectorDocumentJournalDataObject> selection(Selection(journal));
	ASSERT_TRUE(selection->Next());
	const ibValuePtr<ibValueRecordDataObjectRef> object = selection->GetObject();
	ASSERT_TRUE(object);
	EXPECT_TRUE(object->GetMetaObject() == invoice || object->GetMetaObject() == refund);

	// …and the manager is what code reaches as `DocumentJournals.Sales`: its Select() is this selection, of the
	// selection type the journal registers.
	const ibCtorMetaValueType* managerType = journal->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Manager);
	ASSERT_NE(managerType, nullptr);
	ibValue manager = managerType->CreateObject(), selected;
	ASSERT_TRUE(manager.CallAsFunc(manager.FindMethod(wxT("Select")), selected, nullptr, 0));
	EXPECT_EQ(Refs(selected).size(), 3u);
	EXPECT_NE(journal->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Selection), nullptr);
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
	std::vector<ibQueryLowering::ibQueryOutputColumn> described;
	ASSERT_NO_THROW(ibQueryLowering::DescribeOutput(*package.SingleSelect(), {}, described));
	ASSERT_EQ(described.size(), 2u);

	const std::shared_ptr<const ibDocumentJournalQueryable> rows = Rows(journal);
	ASSERT_NE(rows, nullptr);
	const std::shared_ptr<const ibBackendQueryColumn> number = rows->ResolveColumnByName(wxT("Number"));
	ibDataDBComposer composer;
	composer.FromSource(rows);
	composer.Select(rows->GetRefColumn());
	composer.Select(number);
	std::vector<ibQueryLowering::ibQueryOutputColumn> schema;
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

// ⭐ EVERY ROW SAYS WHICH DOCUMENT IT IS - in a column no document has: the type of its reference, a Type, as
// code compares it. Nothing writes it; it is read off the reference's type field, and filtered there.
TEST_F(DocumentJournalFix, EveryRowSaysWhichDocumentItIs) {
	const ibValue anInvoice = Write(invoice, kDay1, ibValue(ibT("INV-1")));
	const ibValue aRefund   = Write(refund, kDay2, ibValue(ibNumber(7)));

	const std::shared_ptr<const ibDocumentJournalQueryable> rows = Rows(journal);
	ASSERT_NE(rows, nullptr);
	EXPECT_TRUE(rows->GetDocumentTypeColumn()->GetTypeDesc().ContainType(g_valueTypeCLSID));
	EXPECT_TRUE(rows->GetDocumentTypeColumn()->IsSyntheticColumn()) << "made of the reference's type field";

	ibDataQueryBuilder read;
	read.From(rows);
	read.Select(rows->GetRefColumn(), wxT("Ref"));
	read.Select(rows->GetDocumentTypeColumn(), wxT("DocumentType"));
	read.OrderBy(rows->GetDateColumn(), true);
	ibDataQueryResult result = read.Execute(ibReadPageRequest());

	std::vector<std::pair<ibValue, ibValue>> said;
	while (result.Next())
		said.emplace_back(result.GetValue(rows->GetRefColumn()), result.GetValue(rows->GetDocumentTypeColumn()));
	ASSERT_EQ(said.size(), 2u);
	for (size_t at = 0; at < said.size(); ++at) {
		const ibValueType* type = CastValue<ibValueType>(said[at].second);
		ASSERT_NE(type, nullptr) << "row " << at << " says its document as a Type";
		EXPECT_EQ(type->GetOwnerTypeClass(), said[at].first.GetClassType()) << "row " << at;
	}
	EXPECT_TRUE(Same(said[0].first, anInvoice));
	EXPECT_TRUE(Same(said[1].first, aRefund));

	// …and a row is chosen by it: the type's class is compared against the reference's type field.
	ibDataQueryBuilder refunds;
	refunds.From(rows);
	refunds.Select(rows->GetRefColumn(), wxT("Ref"));
	refunds.WhereCompare(rows->GetDocumentTypeColumn(), ibQueryFilterOp::Equal, ibValue(new ibValueType(aRefund.GetClassType())));
	ibDataQueryResult chosen = refunds.Execute(ibReadPageRequest());
	std::vector<ibValue> refs;
	while (chosen.Next())
		refs.push_back(chosen.GetValue(rows->GetRefColumn()));
	ASSERT_EQ(refs.size(), 1u);
	EXPECT_TRUE(Same(refs[0], aRefund));
}

// The list form opens over the same source, and a row of it is the document: its value is the reference and
// so is its identity, whole - a guid alone would not say which document's.
TEST_F(DocumentJournalFix, TheListIsTheSameRowsAndARowIsItsDocument) {
	EXPECT_EQ(journal->GetFormType().GetItemCount(), 1u);

	ibQueryableSourceDescriptor* const source = cfg->GetSourceFactory()->FindDescriptor(wxT("DocumentJournal"), wxT("Sales"));
	ASSERT_NE(source, nullptr) << "the journal is a source by its kind and name";
	EXPECT_EQ(source->CreateQueryable(nullptr, 0), journal->GetQueryable()) << "the source reads the journal's own reading";

	const ibValue doc = Write(refund, kDay1, ibValue(ibNumber(7)));
	const ibMetaID refId = Rows(journal)->GetRefColumn()->GetColumnId();
	ibRowMetaValues row;
	row[refId] = doc;
	EXPECT_TRUE(Same(source->GetSelectValue(row), doc));
	EXPECT_TRUE(Same(source->GetItemKey(row).GetKey(refId), doc));
}

// ⭐ A ROW IS COMMANDED AND DRAWN AS ITS DOCUMENT: the journal's list offers what a document's list offers, and a
// row's picture is the one its document's own list draws it with, read off the journal's cells.
TEST_F(DocumentJournalFix, ARowIsCommandedAndDrawnAsItsDocument) {
	ibQueryableSourceDescriptor* const source = cfg->GetSourceFactory()->FindDescriptor(wxT("DocumentJournal"), wxT("Sales"));
	ASSERT_NE(source, nullptr);

	std::vector<ibCommandItem> offered, own;
	source->GetCommandCollection(ibValueMetaObjectDocumentJournal::eFormList, offered);
	invoice->GetCommandCollection(ibValueMetaObjectDocumentJournal::eFormList, own);
	ASSERT_FALSE(own.empty());
	ASSERT_EQ(offered.size(), own.size());
	for (size_t i = 0; i < own.size(); ++i)
		EXPECT_EQ(offered[i].m_actionId, own[i].m_actionId) << "command " << i;

	const auto idOf = [this](const ibString& name) {
		for (const std::shared_ptr<const ibBackendQueryColumn>& column : Rows(journal)->GetQueryColumns())
			if (column->GetName() == name)
				return column->GetColumnId();
		return ibMetaID(0);
	};
	ibRowMetaValues row;
	row[idOf(ibT("Ref"))] = Write(refund, kDay1, ibValue(ibNumber(7)));
	row[idOf(ibT("Posted"))] = ibValue(true);
	row[idOf(ibT("DeletionMark"))] = ibValue(false);
	EXPECT_EQ(source->GetRowPicture(row), g_picRowDocumentPostedCLSID);
	row[idOf(ibT("DeletionMark"))] = ibValue(true);
	EXPECT_EQ(source->GetRowPicture(row), g_picRowDocumentDeletedCLSID) << "the mark is asked first, as the document asks it";
}

// A journal of no document is not saved, and says which journal; one that lists documents is.
TEST_F(DocumentJournalFix, AJournalOfNothingIsNotSaved) {
	ibRestructureInfo& ledger = ibMetaDataApplicationBase::GetRestructureInfo();
	ledger.Clear();
	EXPECT_FALSE(nothing->OnSaveMetaObject(0));
	ibString said;
	for (const ibRestructureInfo::Entry& line : ledger)
		said += line.descr;
	ledger.Clear();
	EXPECT_NE(said.Find(ibT("Nothing")), wxNOT_FOUND) << said.ToStdString();

	EXPECT_TRUE(journal->OnSaveMetaObject(0));
	EXPECT_TRUE(ledger.IsEmpty()) << "a journal of documents is saved without a word";
}

// A journal that lists nothing has no rows - and still the columns, answered in memory rather than as a
// statement over no table.
TEST_F(DocumentJournalFix, AJournalOfNothingHasNoRows) {
	const std::shared_ptr<const ibDocumentJournalQueryable> rows = Rows(nothing);
	ASSERT_NE(rows, nullptr);
	EXPECT_TRUE(rows->GetProvider().IsComputedInRam());
	EXPECT_EQ(rows->GetQueryColumns().size(), 6u);
	EXPECT_TRUE(Refs(Selection(nothing)).empty());
}

// ⭐ THE JOURNAL LISTS WHAT ITS READER MAY READ (Max, 2026-11-24): a document the role may not read is refused
// when read directly, and in the journal it is simply not among the rows - the rest is shown. The journal is
// no road around a right, and no wall in front of the documents one may read either.
TEST_F(DocumentJournalFix, ADocumentTheReaderMayNotReadIsLeftOut) {
	const ibValue shown = Write(invoice, kDay1, ibValue(ibT("INV-1")));
	Write(refund, kDay2, ibValue(ibNumber(7)));
	ASSERT_TRUE(journal->SetRight(wxT("Read"), role->GetMetaID(), true));
	ASSERT_TRUE(invoice->SetRight(wxT("Read"), role->GetMetaID(), true));
	ASSERT_TRUE(refund->SetRight(wxT("Read"), role->GetMetaID(), false));

	const std::shared_ptr<ibSession> session = SessionInRole(ibString());
	ASSERT_NE(session, nullptr);
	const RootCleared cleared{ session.get() };
	ibSessionScope bound(session.get());
	ASSERT_NE(session->GetAccessPolicy(), nullptr) << "a user in a restricting role read under no policy";

	// The refund alone is refused…
	ibDataQueryBuilder alone;
	alone.From(refund->GetQueryable());
	EXPECT_THROW((void)alone.Execute(ibReadPageRequest()), ibBackendAccessException);

	// …and the journal over it and the invoice shows the invoice.
	const std::vector<ibValue> refs = Refs(Selection(journal));
	ASSERT_EQ(refs.size(), 1u);
	EXPECT_TRUE(Same(refs.front(), shown));
}

// No document it lists is readable: the journal is read and has no rows - not a refusal, since its own right
// lets it be read.
TEST_F(DocumentJournalFix, NoDocumentTheReaderMayReadLeavesTheJournalEmpty) {
	Write(invoice, kDay1, ibValue(ibT("INV-1")));
	Write(refund, kDay2, ibValue(ibNumber(7)));
	ASSERT_TRUE(journal->SetRight(wxT("Read"), role->GetMetaID(), true));
	ASSERT_TRUE(invoice->SetRight(wxT("Read"), role->GetMetaID(), false));
	ASSERT_TRUE(refund->SetRight(wxT("Read"), role->GetMetaID(), false));

	const std::shared_ptr<ibSession> session = SessionInRole(ibString());
	ASSERT_NE(session, nullptr);
	const RootCleared cleared{ session.get() };
	ibSessionScope bound(session.get());

	std::vector<ibValue> refs;
	EXPECT_NO_THROW(refs = Refs(Selection(journal)));
	EXPECT_TRUE(refs.empty());
}

// ⭐ THE JOURNAL HAS A RIGHT OF ITS OWN: a role that may read every document it lists but not the journal is
// refused the journal - who may open it is said on it, as on any list in the interface.
TEST_F(DocumentJournalFix, AJournalItsReaderMayNotReadIsRefused) {
	Write(invoice, kDay1, ibValue(ibT("INV-1")));
	ASSERT_TRUE(journal->SetRight(wxT("Read"), role->GetMetaID(), false));
	ASSERT_TRUE(invoice->SetRight(wxT("Read"), role->GetMetaID(), true));
	ASSERT_TRUE(refund->SetRight(wxT("Read"), role->GetMetaID(), true));

	const std::shared_ptr<ibSession> session = SessionInRole(ibString());
	ASSERT_NE(session, nullptr);
	const RootCleared cleared{ session.get() };
	ibSessionScope bound(session.get());

	EXPECT_THROW((void)Refs(Selection(journal)), ibBackendAccessException);
}

// ⭐ A READ UNDER A POLICY IS STILL A RELATION, its reader's rights folded into it: refused, it raises as a read does;
// read AS ALLOWED, it is a relation of no row - the branch of a union the reader may not read. It was null, and whoever
// wanted a relation switched the policy off and asked the rights by hand.
TEST_F(DocumentJournalFix, ARelationUnderAPolicyCarriesItsRights) {
	ASSERT_TRUE(invoice->SetRight(wxT("Read"), role->GetMetaID(), true));
	ASSERT_TRUE(refund->SetRight(wxT("Read"), role->GetMetaID(), false));

	const std::shared_ptr<ibSession> session = SessionInRole(ibString());
	ASSERT_NE(session, nullptr);
	const RootCleared cleared{ session.get() };
	ibSessionScope bound(session.get());
	ASSERT_NE(session->GetAccessPolicy(), nullptr) << "a user in a restricting role read under no policy";

	ibDataQueryBuilder open;
	open.From(invoice->GetQueryable());
	EXPECT_NE(open.BuildRelation(), nullptr);

	ibDataQueryBuilder closed;
	closed.From(refund->GetQueryable());
	EXPECT_THROW((void)closed.BuildRelation(), ibBackendAccessException);

	ibDataQueryBuilder allowed;
	allowed.Allowed().From(refund->GetQueryable());
	EXPECT_NE(allowed.BuildRelation(), nullptr);
}

// ⭐ A DECLARED QUERY IS READ UNDER ITS READER'S RIGHTS - the road every composed report's source takes (`q_sub0`): its
// body is written into the statement, and the read above it sees a name. It was written unrestricted, and read what the
// role keeps back.
TEST_F(DocumentJournalFix, ADeclaredQueryIsReadUnderItsReadersRights) {
	Write(invoice, kDay1, ibValue(ibT("INV-1")));
	Write(invoice, kDay2, ibValue(ibT("INV-2")));
	ASSERT_TRUE(invoice->SetRight(wxT("Read"), role->GetMetaID(), true));
	ASSERT_TRUE(refund->SetRight(wxT("Read"), role->GetMetaID(), false));

	const std::shared_ptr<ibSession> session = SessionInRole(kOneInvoice);
	ASSERT_NE(session, nullptr);
	const RootCleared cleared{ session.get() };
	ibSessionScope bound(session.get());
	ASSERT_NE(session->GetAccessPolicy(), nullptr) << "a user in a restricting role read under no policy";

	// The invoices, declared and read through the declaration - published as the reference itself, as a declaration
	// publishes a source's own column.
	const std::shared_ptr<const ibBackendQueryColumn> ref = invoice->GetDataReference()->GetQueryColumn();
	ibDataQueryBuilder body;
	body.From(invoice->GetQueryable()).Select(ref, ref->GetName());
	ibCteQueryable::Field field;
	field.m_borrowed = ref;
	field.m_isKey = true;
	const auto declared = std::make_shared<ibCteQueryable>(wxT("q_invoices"), std::vector<ibCteQueryable::Field>{ field }, 1, cfg.get());
	ibDataQueryBuilder statement;
	statement.With(wxT("q_invoices"), body).From(declared);
	ibDataQueryResult rows = statement.Execute(ibReadPageRequest());
	size_t count = 0;
	while (rows.Next())
		++count;
	EXPECT_EQ(count, 1u) << "the role keeps INV-2 back, and the declaration read it";

	// …and a document the reader may not read is refused where it is declared.
	ibDataQueryBuilder closed;
	closed.From(refund->GetQueryable());
	ibDataQueryBuilder over;
	EXPECT_THROW(over.With(wxT("q_refunds"), closed), ibBackendAccessException);
}

// ⭐ AN IN (SELECT …) IS READ UNDER ITS READER'S RIGHTS - said as a semi-join's EXISTS, its read is the door's
// (BuildReadPredicate), folded as a relation is; said as a set, it is read as any read. The role keeps INV-2 back: the
// refund dated as INV-1 is matched, the one dated as INV-2 is not.
TEST_F(DocumentJournalFix, AnInSubqueryIsReadUnderItsReadersRights) {
	Write(invoice, kDay1, ibValue(ibT("INV-1")));
	Write(invoice, kDay2, ibValue(ibT("INV-2")));
	Write(refund, kDay1, ibValue(ibNumber(7)));
	Write(refund, kDay2, ibValue(ibNumber(8)));
	ASSERT_TRUE(invoice->SetRight(wxT("Read"), role->GetMetaID(), true));
	ASSERT_TRUE(refund->SetRight(wxT("Read"), role->GetMetaID(), true));

	const std::shared_ptr<ibSession> session = SessionInRole(kOneInvoice);
	ASSERT_NE(session, nullptr);
	const RootCleared cleared{ session.get() };
	ibSessionScope bound(session.get());
	ASSERT_NE(session->GetAccessPolicy(), nullptr) << "a user in a restricting role read under no policy";

	EXPECT_EQ(RowsOf(wxT("SELECT Number FROM Document.Refund WHERE Date IN (SELECT Date FROM Document.Invoice)")), 1)
		<< "the role keeps INV-2 back, and the refund dated as it was matched";
}

// ⭐ SELECT ALLOWED COVERS AN IN (SELECT …) - said once, for the whole query: the invoices closed to the reader are
// the set of no row, and the query reads the refunds none matches rather than being refused whole (2026-11-24).
TEST_F(DocumentJournalFix, AllowedReachesAnInSubquery) {
	Write(invoice, kDay1, ibValue(ibT("INV-1")));
	Write(refund, kDay1, ibValue(ibNumber(7)));
	ASSERT_TRUE(invoice->SetRight(wxT("Read"), role->GetMetaID(), false));
	ASSERT_TRUE(refund->SetRight(wxT("Read"), role->GetMetaID(), true));

	const std::shared_ptr<ibSession> session = SessionInRole(kOneInvoice);
	ASSERT_NE(session, nullptr);
	const RootCleared cleared{ session.get() };
	ibSessionScope bound(session.get());

	EXPECT_EQ(RowsOf(wxT("SELECT ALLOWED Number FROM Document.Refund WHERE Date IN (SELECT Date FROM Document.Invoice)")), 0)
		<< "the closed invoices matched a refund";
	EXPECT_EQ(RowsOf(wxT("SELECT ALLOWED Number FROM Document.Refund WHERE Date NOT IN (SELECT Date FROM Document.Invoice)")), 1)
		<< "the closed invoices kept a refund out of NOT IN";
}

// ⭐ A DOCUMENT'S ROW FILTER HOLDS IN THE JOURNAL: the invoices the role keeps back are not among the journal's
// rows, while the refund, which it does not filter, comes whole.
TEST_F(DocumentJournalFix, ARowFilterOnADocumentHoldsInTheJournal) {
	const ibValue kept    = Write(invoice, kDay1, ibValue(ibT("INV-1")));
	Write(invoice, kDay2, ibValue(ibT("INV-2")));
	const ibValue another = Write(refund, kDay3, ibValue(ibNumber(7)));
	ASSERT_TRUE(journal->SetRight(wxT("Read"), role->GetMetaID(), true));
	ASSERT_TRUE(invoice->SetRight(wxT("Read"), role->GetMetaID(), true));
	ASSERT_TRUE(refund->SetRight(wxT("Read"), role->GetMetaID(), true));

	const std::shared_ptr<ibSession> session = SessionInRole(kOneInvoice);
	ASSERT_NE(session, nullptr);
	const RootCleared cleared{ session.get() };
	ibSessionScope bound(session.get());
	ASSERT_NE(session->GetAccessPolicy(), nullptr) << "a user in a restricting role read under no policy";

	const std::vector<ibValue> refs = Refs(Selection(journal));
	ASSERT_EQ(refs.size(), 2u);
	for (const ibValue& shown : { kept, another })
		EXPECT_NE(std::find_if(refs.begin(), refs.end(), [&shown](const ibValue& ref) { return Same(ref, shown); }), refs.end());
}

// ⭐ OVER MCP, AS AN ASSISTANT BUILDS ONE: `metadata_create` makes the journal and the documents, `metadata_bind`
// lists them in RegisteredDocuments, and the journal reads them - the same binding the designer's property grid
// sets. The application is the one a designer session edits (test_applicationCheckRegisters.cpp says why each
// step is there).
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
	ibMetaDataApplicationStorage::CreateApplicationTable();
	ibMetaDataApplicationStorage::CreateApplicationSaveTable();
	ibMetaDataApplicationStorage::CreateApplicationSequence();

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

	ibMetaDataApplicationBase* cfg = session->GetMetaData();
	ASSERT_NE(cfg, nullptr);
	if (!cfg->IsOpen())
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

	// The binding is a property change, and another list of documents is another reading, typed by them.
	const std::shared_ptr<const ibDocumentJournalQueryable> rows =
		std::dynamic_pointer_cast<const ibDocumentJournalQueryable>(journal->GetQueryable());
	ASSERT_NE(rows, nullptr);
	EXPECT_EQ(rows->GetRefColumn()->GetTypeDesc().GetClsidCount(), 2u) << "a reference to either document";
}
