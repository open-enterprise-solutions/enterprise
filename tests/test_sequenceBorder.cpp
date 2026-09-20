// =============================================================================
// THE BORDER OF A SEQUENCE, MOVED BY THE ENGINE'S OWN RULES — live, on SQLite.
//
// A sequence in a configuration in memory, its two tables made out of the fields the metaobject itself
// lays out, registrations written the way a posting writes them, and the two doors the write path knocks
// on (ibSequenceBorderWritten / ibSequenceBorderCleared) called after each.
//
// 🛑 WHAT THIS PINS: A RETREAT LANDS ON THE REGISTRATION BEFORE, NOT NOWHERE. `PeriodBefore` projected one
// field - MAX over the period's date - and read it through the column's codec, which reads a TAGGED cell;
// with no `_TYPE` beside it the codec answers "field not in the result set" with the type's EMPTY value, and
// an empty date read as "nothing stands before this document". Every retreat that crossed into an earlier
// period took the border AWAY: a document posted behind the border, a posting undone, and even posting
// again the very document the border stood on. A border that is gone is never wrong - nothing is vouched
// for - but it sends the restoring run over the key's whole history (2026-09-20).
// =============================================================================

#include <gtest/gtest.h>

#include <set>
#include <vector>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/compiler/value.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/connectionHolder.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/dimension/metaDimensionObject.h"
#include "backend/metaCollection/partial/sequence.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/query/columnLayout.h"
#include "backend/system/value/valuePointInTime.h"

namespace {

struct SequenceBorderFix : ::testing::Test {
	wxInitializer                          m_wxInit;
	std::shared_ptr<ibDatabaseLayerSQLite> db;

	std::unique_ptr<ibMetaDataConfigurationFile> cfg;
	ibValueMetaObject*          document = nullptr;
	ibValueMetaObjectSequence*  seq      = nullptr;
	int                         line     = 0;

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";
		if (!ibApplicationData::CreateAppDataEnv(ibRunMode::eRUNTIME_MODE))
			GTEST_SKIP() << "appData env unavailable headless";
		ibConnectionPool* pool = ibApplicationData::GetConnectionPool();
		if (pool == nullptr)
			GTEST_SKIP() << "no connection pool after CreateAppDataEnv";
		db = std::make_shared<ibDatabaseLayerSQLite>();
		if (!db->Open(wxT(":memory:")))
			GTEST_SKIP() << "in-memory SQLite open failed";
		pool->Init(db, /*maxSize=*/1, /*minIdle=*/0);

		cfg = std::make_unique<ibMetaDataConfigurationFile>();
		ibValueMetaObjectConfiguration* root = cfg->GetCommonMetaObject();
		ASSERT_NE(root, nullptr);
		document = cfg->CreateMetaObject(g_metaDocumentCLSID, root, /*runObject*/ false);
		seq = dynamic_cast<ibValueMetaObjectSequence*>(cfg->CreateMetaObject(g_metaSequenceCLSID, root, false));
		ASSERT_NE(document, nullptr);
		ASSERT_NE(seq, nullptr);

		// One dimension - "the warehouse" - held as a string, and the document as the recorder: what binding
		// a document to the sequence does (documentMetadataProperty.cpp), said directly.
		ibValueMetaObject* dimension = cfg->CreateMetaObject(g_metaDimensionCLSID, seq, false);
		ASSERT_NE(dimension, nullptr);
		ASSERT_EQ(seq->GetDimensionArrayObject().size(), 1u);
		seq->GetDimensionArrayObject().front()->GetTypeDesc().SetDefaultMetaType(ibValueTypes::TYPE_STRING);
		seq->GetRegisterRecorder()->GetTypeDesc().AppendMetaType(reference_to_clsid(document->GetMetaID()));
		ASSERT_TRUE(seq->HasBorders()) << "the borders' holder is numbered with the sequence";

		// ⭐ RUN IT. A reference read back from a row is created through the configuration's TYPE registry,
		// and that registry exists only while the configuration runs: left unrun, a registration's recorder
		// reads back empty and the border is written with nobody standing on it.
		ASSERT_TRUE(cfg->RunDatabase()) << "the configuration in memory did not run";

		// The two tables, made of exactly the fields the metaobject lays out - SQLite asks for no types.
		std::vector<const ibBackendQueryColumn*> registration = {
			seq->GetRegisterActive()->GetQueryColumn(), seq->GetRegisterPeriod()->GetQueryColumn(),
			seq->GetRegisterRecorder()->GetQueryColumn(), seq->GetRegisterLineNumber()->GetQueryColumn(),
			Dimension() };
		std::vector<const ibBackendQueryColumn*> border = {
			Dimension(), seq->GetRegisterPeriod()->GetQueryColumn(), seq->GetRegisterRecorder()->GetQueryColumn() };

		db->RunQuery(wxT("%s"), wxT("CREATE TABLE ") + seq->GetQueryable()->GetQueryTableName()
			+ wxT(" (") + FieldList(registration, true) + wxT(")"));
		db->RunQuery(wxT("%s"), wxT("CREATE TABLE ") + seq->GetBordersTableName()
			+ wxT(" (") + FieldList(border, true) + wxT(", UNIQUE (") + FieldList({ Dimension() }) + wxT("))"));
	}
	void TearDown() override {
		if (cfg) cfg->CloseDatabase();
		cfg.reset();
		if (ibApplicationData::Get() != nullptr)
			ibApplicationData::DestroyAppDataEnv();
	}

	const ibBackendQueryColumn* Dimension() const {
		return seq->GetDimensionArrayObject().front()->GetQueryColumn();
	}
	static wxString FieldList(const std::vector<const ibBackendQueryColumn*>& columns, bool typed = false) {
		wxString list;
		std::set<wxString> seen;
		for (const ibBackendQueryColumn* column : columns)
			for (const wxString& field : ColumnFieldNames(column))
				if (seen.insert(field).second)
					list << (list.IsEmpty() ? wxT("") : wxT(", ")) << field << (typed ? SqliteTypeOf(field) : wxString());
		return list;
	}
	// Declared by what each field holds, as the platform's own DDL declares them - a table of untyped
	// columns would be a table no configuration ever has.
	static wxString SqliteTypeOf(const wxString& field) {
		if (field.EndsWith(wxT("_RRRef"))) return wxT(" BLOB");
		if (field.EndsWith(wxT("_D")) || field.EndsWith(wxT("_S"))) return wxT(" TEXT");
		if (field.EndsWith(wxT("_N"))) return wxT(" NUMERIC");
		return wxT(" INTEGER");   // _TYPE, _RTRef, _B
	}

	// A value said as the fields that hold it - the same codec the rules write the border through.
	void Assign(std::vector<ibDmlAssign>& out, const ibBackendQueryColumn* column, const ibValue& value) const {
		const std::vector<wxString> fields = ColumnFieldNames(column);
		ibQueryStatement capture(ibQueryStatement::Kind::Delete, wxString(), fields);
		int position = 1;
		ibColumnCodec::WriteValue(column, cfg.get(), value, &capture, position);
		const std::vector<ibQueryExprPtr>& consts = capture.CapturedValues();
		for (size_t i = 0; i < fields.size(); ++i)
			out.push_back(ibDmlAssign{ fields[i], (i < consts.size() && consts[i]) ? consts[i] : ibConst(ibValue()) });
	}

	ibValue NewDocument() const {
		return ibValue(ibValueReferenceDataObject::Create(cfg.get(), document->GetMetaID(), ibGuid(ibGuid::newGuid())));
	}
	static ibValue Moment(const wxDateTime& when, const ibValue& recorder) {
		return ibValue(new ibValuePointInTime(when, recorder));
	}

	// What a posting does: the document's registration is written, then the border is told.
	void Post(const ibValue& recorder, const wxDateTime& when, const wxString& warehouse) {
		std::vector<ibDmlAssign> row;
		Assign(row, seq->GetRegisterActive()->GetQueryColumn(), ibValue(true));
		Assign(row, seq->GetRegisterPeriod()->GetQueryColumn(), ibValue(when));
		Assign(row, seq->GetRegisterRecorder()->GetQueryColumn(), recorder);
		Assign(row, seq->GetRegisterLineNumber()->GetQueryColumn(), ibValue(ibNumber(++line)));
		Assign(row, Dimension(), ibValue(warehouse));
		ibDatabaseQueryBuilder insert;
		ASSERT_GE(insert.Execute(ibInsert(seq->GetQueryable()->GetQueryTableName(), row)), 0);
		ibSequenceBorderWritten(seq, recorder);
	}
	// …and what posting it AGAIN does first: the border is told, then the rows go.
	void Clear(const ibValue& recorder) {
		ibSequenceBorderCleared(seq, recorder);
		std::vector<ibDmlAssign> key;
		Assign(key, seq->GetRegisterRecorder()->GetQueryColumn(), recorder);
		ibQueryExprPtr where;
		for (const ibDmlAssign& one : key) {
			const ibQueryExprPtr term = ibBinOp(ibQueryBinOp::Eq, ibCol(one.m_column), one.m_value);
			where = where ? ibBinOp(ibQueryBinOp::And, where, term) : term;
		}
		ibDatabaseQueryBuilder drop;
		ASSERT_GE(drop.Execute(ibDelete(seq->GetQueryable()->GetQueryTableName(), where)), 0);
	}

	ibValue Border(const wxString& warehouse) const {
		return ibSequenceBorderGet(seq, { ibValue(warehouse) });
	}
	static bool Same(const ibValue& a, const ibValue& b) {
		return !a.IsEmpty() && !b.IsEmpty() && a.CompareValueLS(b) == 0 && b.CompareValueLS(a) == 0;
	}
};

const wxDateTime kDay1(1, wxDateTime::Jul, 2025, 10, 0, 0);
const wxDateTime kDay1Later(1, wxDateTime::Jul, 2025, 15, 0, 0);
const wxDateTime kDay2(2, wxDateTime::Jul, 2025, 10, 0, 0);
const wxDateTime kDay3(3, wxDateTime::Jul, 2025, 10, 0, 0);

} // namespace

// Posted in their turn, each document takes the border one step forward - and another warehouse's border
// is its own.
TEST_F(SequenceBorderFix, PostedInTurnTheBorderStepsForward) {
	const ibValue r1 = NewDocument(), r2 = NewDocument(), other = NewDocument();
	Post(r1, kDay1, wxT("kitchen"));
	EXPECT_TRUE(Same(Border(wxT("kitchen")), Moment(kDay1, r1)));
	Post(r2, kDay2, wxT("kitchen"));
	EXPECT_TRUE(Same(Border(wxT("kitchen")), Moment(kDay2, r2)));

	Post(other, kDay1, wxT("bar"));
	EXPECT_TRUE(Same(Border(wxT("bar")), Moment(kDay1, other)));
	EXPECT_TRUE(Same(Border(wxT("kitchen")), Moment(kDay2, r2)));
}

// 🛑 THE DEFECT. R1, R2, R3 posted in turn, then a document dated between R1 and R2: everything after it is
// in doubt, R1 is not - the border goes back to R1. It went AWAY.
TEST_F(SequenceBorderFix, ADocumentPostedBehindTheBorderSendsItToTheRegistrationBefore) {
	const ibValue r1 = NewDocument(), r2 = NewDocument(), r3 = NewDocument(), late = NewDocument();
	Post(r1, kDay1, wxT("kitchen"));
	Post(r2, kDay2, wxT("kitchen"));
	Post(r3, kDay3, wxT("kitchen"));
	ASSERT_TRUE(Same(Border(wxT("kitchen")), Moment(kDay3, r3)));

	Post(late, kDay1Later, wxT("kitchen"));
	const ibValue border = Border(wxT("kitchen"));
	ASSERT_FALSE(border.IsEmpty()) << "the border was taken away instead of back";
	EXPECT_TRUE(Same(border, Moment(kDay1, r1)));
}

// 🛑 …and the commonest act there is: posting again the very document the border stands on. Its rows are
// cleared (the border steps back to R2) and written again (it steps forward onto R3) - it ends where it was.
TEST_F(SequenceBorderFix, PostingAgainTheDocumentTheBorderStandsOnKeepsTheBorder) {
	const ibValue r1 = NewDocument(), r2 = NewDocument(), r3 = NewDocument();
	Post(r1, kDay1, wxT("kitchen"));
	Post(r2, kDay2, wxT("kitchen"));
	Post(r3, kDay3, wxT("kitchen"));

	Clear(r3);
	EXPECT_TRUE(Same(Border(wxT("kitchen")), Moment(kDay2, r2))) << "cleared: back to the registration before";
	Post(r3, kDay3, wxT("kitchen"));
	EXPECT_TRUE(Same(Border(wxT("kitchen")), Moment(kDay3, r3))) << "written again: forward onto it";
}

// A retreat with nothing before it is the one case where the border really goes away.
TEST_F(SequenceBorderFix, WithNothingBeforeItTheBorderGoesAway) {
	const ibValue r1 = NewDocument();
	Post(r1, kDay1, wxT("kitchen"));
	ASSERT_FALSE(Border(wxT("kitchen")).IsEmpty());
	Clear(r1);
	EXPECT_TRUE(Border(wxT("kitchen")).IsEmpty());
}

// The restoring run: the documents after the border posted again in their order walk it to the end.
TEST_F(SequenceBorderFix, RepostingInOrderWalksTheBorderToTheEnd) {
	const ibValue r1 = NewDocument(), r2 = NewDocument(), r3 = NewDocument(), late = NewDocument();
	Post(r1, kDay1, wxT("kitchen"));
	Post(r2, kDay2, wxT("kitchen"));
	Post(r3, kDay3, wxT("kitchen"));
	Post(late, kDay1Later, wxT("kitchen"));   // the border is back at R1

	Clear(late); Post(late, kDay1Later, wxT("kitchen"));
	EXPECT_TRUE(Same(Border(wxT("kitchen")), Moment(kDay1Later, late)));
	Clear(r2); Post(r2, kDay2, wxT("kitchen"));
	EXPECT_TRUE(Same(Border(wxT("kitchen")), Moment(kDay2, r2)));
	Clear(r3); Post(r3, kDay3, wxT("kitchen"));
	EXPECT_TRUE(Same(Border(wxT("kitchen")), Moment(kDay3, r3)));
}

// The driver itself, asked directly. A whole number a double cannot carry - a kind-typed class id is sixty bits -
// goes in as an integer and comes back whole. A whole number a double DOES carry goes in the way it always
// did, as a real: bound as an INTEGER it would turn `10 / ?` into SQLite's integer division and answer 2.
TEST(SqliteNumberBinding, OnlyANumberADoubleCannotCarryIsBoundAsAnInteger) {
	auto db = std::make_shared<ibDatabaseLayerSQLite>();
	ASSERT_TRUE(db->Open(wxT(":memory:")));

	const long long sixtyBits = 1152921504606846977LL;   // 2^60 + 1: the +1 is what a double drops
	ibPreparedStatement* statement = db->PrepareStatement(wxT("SELECT 10 / ?, ?"));
	ASSERT_NE(statement, nullptr);
	statement->SetParamNumber(1, ibNumber(4LL));
	statement->SetParamNumber(2, ibNumber(sixtyBits));
	ibDatabaseResultSet* rs = statement->RunQueryWithResults();
	ASSERT_NE(rs, nullptr);
	ASSERT_TRUE(rs->Next());
	EXPECT_DOUBLE_EQ(rs->GetResultDouble(1), 2.5);
	EXPECT_EQ(rs->GetResultLong(2), sixtyBits);
	statement->CloseResultSet(rs);
	db->CloseStatement(statement);
}
