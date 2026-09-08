// =============================================================================
// OES Enterprise — value-table column indexes
//
// A value table (ibValueModelTable, backend/system/value/valueTable.h) owns its
// rows in RAM. Until 2026-09-08 its ONLY lookup was a scan: Find(value, column)
// walked every row and compared, and so did any equality filter written over the
// table — N comparisons over N rows, with a linear IndexOf on top whenever a row
// was fetched positionally.
//
// ⭐ THE INDEX LIVES ON THE MODEL THAT HOLDS THE ROWS — `ibValueModelStorage` (tabularModel.h) —
// and not on the value table, because a tabular section and a register's record set keep their rows
// in the very same storage and are searched the same way. What each of them decides is only whether
// it is ready to use one; the machinery is maintained at the top. These tests exercise it through
// the value table because that is the one place a test can build rows without a configuration.
//
// An index is declared PER COLUMN (`Indexing`), built lazily on first use, and
// rebuilt when the model's own change counter has moved. These tests pin the
// three things that make it trustworthy rather than merely fast:
//
//   1. an indexed column answers the same row a scan would have answered — the
//      FIRST match in row order, not an arbitrary one;
//   2. it stays correct across mutation, because there is no invalidation call
//      site to forget: the index carries the generation it was built at;
//   3. a column that declared no index still answers, by scanning.
//
// Pure RAM — no database, no session.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/system/value/valueTable.h"

namespace {

// A table with one indexed column and one that is not, so every case below can
// ask the same question down both roads and compare the answers.
class ValueTableIndex : public ::testing::Test {
protected:
    // 🛑 HEAP, HELD BY AN ibValue — see test_valueTableWalk.cpp for the whole reason. The model is
    // reference counted and a row takes a reference on it, so an instance that starts at a count of
    // zero is deleted the first time a row is released. Nothing here yields a row today, which is
    // why this file was green while its neighbour corrupted the heap; the difference is one test
    // away, and a fixture that is only correct as long as nobody asks for a row is not correct.
    void SetUp() override {
        m_table = new ibValueModelTable();
        m_holder = m_table;

        m_code = m_table->GetColumnCollection()->AddColumn(
            wxT("Code"), ibTypeDescription(), wxT("Code"));
        m_name = m_table->GetColumnCollection()->AddColumn(
            wxT("Name"), ibTypeDescription(), wxT("Name"));
        ASSERT_NE(m_code, nullptr);
        ASSERT_NE(m_name, nullptr);
        m_code->SetColumnIndexed(true);      // Code is searched; Name is not
    }

    // Appends a row and writes both cells. Returns the row's item.
    ibDataViewItem AddRow(const wxString& code, const wxString& name) {
        const long row = m_table->AppendRow();
        const ibDataViewItem item = m_table->GetItem(row);
        ibValueModel::ibComposerNode* node =
            m_table->GetViewData<ibValueModel::ibComposerNode>(item);
        EXPECT_NE(node, nullptr);
        if (node != nullptr) {
            node->SetValue((ibMetaID)m_code->GetColumnID(), ibValue(code));
            node->SetValue((ibMetaID)m_name->GetColumnID(), ibValue(name));
        }
        return item;
    }

    ibValue            m_holder;             // keeps the count above zero for the fixture's life
    ibValueModelTable* m_table = nullptr;    // owned by m_holder, never deleted by hand
    ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo* m_code = nullptr;
    ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo* m_name = nullptr;
};

} // namespace

TEST_F(ValueTableIndex, DeclaredOnTheColumn) {
    EXPECT_TRUE(m_code->IsColumnIndexed());
    EXPECT_FALSE(m_name->IsColumnIndexed());
}

TEST_F(ValueTableIndex, FindsTheSameRowAsAScan) {
    AddRow(wxT("A"), wxT("first"));
    const ibDataViewItem wanted = AddRow(wxT("B"), wxT("second"));
    AddRow(wxT("C"), wxT("third"));

    // Indexed column and unindexed column answer the same shape of question.
    EXPECT_EQ(m_table->FindRowValue(ibValue(wxString(wxT("B"))), wxT("Code")), wanted);
    EXPECT_EQ(m_table->FindRowValue(ibValue(wxString(wxT("second"))), wxT("Name")), wanted);
}

TEST_F(ValueTableIndex, MissAnswersNoRow) {
    AddRow(wxT("A"), wxT("first"));
    EXPECT_FALSE(m_table->FindRowValue(ibValue(wxString(wxT("Z"))), wxT("Code")).IsOk());
    EXPECT_FALSE(m_table->FindRowValue(ibValue(wxString(wxT("Z"))), wxT("Name")).IsOk());
}

// A column is not a key: equal cells are ordinary, and the answer is the FIRST
// such row — the one the scan would have stopped on.
TEST_F(ValueTableIndex, DuplicateCellsAnswerTheFirstRow) {
    const ibDataViewItem first = AddRow(wxT("SAME"), wxT("one"));
    AddRow(wxT("SAME"), wxT("two"));
    EXPECT_EQ(m_table->FindRowValue(ibValue(wxString(wxT("SAME"))), wxT("Code")), first);
}

// The point of building the index against the model's change counter: nothing
// has to remember to invalidate it.
TEST_F(ValueTableIndex, SeesARowAddedAfterTheFirstLookup) {
    AddRow(wxT("A"), wxT("first"));
    EXPECT_TRUE(m_table->FindRowValue(ibValue(wxString(wxT("A"))), wxT("Code")).IsOk());   // builds it

    const ibDataViewItem added = AddRow(wxT("B"), wxT("second"));
    EXPECT_EQ(m_table->FindRowValue(ibValue(wxString(wxT("B"))), wxT("Code")), added);
}

TEST_F(ValueTableIndex, SeesACellChangedAfterTheFirstLookup) {
    const ibDataViewItem row = AddRow(wxT("OLD"), wxT("first"));
    EXPECT_TRUE(m_table->FindRowValue(ibValue(wxString(wxT("OLD"))), wxT("Code")).IsOk());

    ibValueModel::ibComposerNode* node =
        m_table->GetViewData<ibValueModel::ibComposerNode>(row);
    ASSERT_NE(node, nullptr);
    node->SetValue((ibMetaID)m_code->GetColumnID(), ibValue(wxString(wxT("NEW"))));
    m_table->RowValueChanged(node, m_code->GetColumnID());

    EXPECT_EQ(m_table->FindRowValue(ibValue(wxString(wxT("NEW"))), wxT("Code")), row);
    EXPECT_FALSE(m_table->FindRowValue(ibValue(wxString(wxT("OLD"))), wxT("Code")).IsOk());
}

TEST_F(ValueTableIndex, SeesRowsCleared) {
    AddRow(wxT("A"), wxT("first"));
    EXPECT_TRUE(m_table->FindRowValue(ibValue(wxString(wxT("A"))), wxT("Code")).IsOk());

    m_table->Clear();
    EXPECT_FALSE(m_table->FindRowValue(ibValue(wxString(wxT("A"))), wxT("Code")).IsOk());
}

// Turning the declaration off puts the column back on the scan, and the answer
// does not change — which is the whole contract: an index is an optimisation,
// never a different result.
TEST_F(ValueTableIndex, TheAnswerDoesNotDependOnBeingIndexed) {
    AddRow(wxT("A"), wxT("first"));
    const ibDataViewItem wanted = AddRow(wxT("B"), wxT("second"));

    const ibDataViewItem indexed = m_table->FindRowValue(ibValue(wxString(wxT("B"))), wxT("Code"));
    m_code->SetColumnIndexed(false);
    const ibDataViewItem scanned = m_table->FindRowValue(ibValue(wxString(wxT("B"))), wxT("Code"));

    EXPECT_EQ(indexed, wanted);
    EXPECT_EQ(scanned, wanted);
}
