// =============================================================================
// OES Enterprise — walking a value table's rows
//
// Iterating a value table used to go through the paged fetch, whatever the table
// looked like: every batch of 64 rows recomputed the whole display order and
// scanned it twice, and each row fetched positionally cost a linear IndexOf on
// top. That is O(n²) for a walk, and it showed — 20 000 rows took eleven seconds
// to read, in a language where reading a table is the ordinary thing to do.
//
// A table with NO filter, NO sort and NO grouping has nothing to arrange: the
// display order IS the storage order, so the walk reads the rows where they
// stand (`ibValueModelRowWalk`, backend/tabularModel.cpp). Any arrangement at
// all and the question goes back to the composer, which is the only thing that
// can answer it.
//
// These tests pin what makes that trustworthy rather than merely fast:
//
//   1. the walk yields EVERY row, in the order they were appended;
//   2. each yielded row is the row itself — its cells read back;
//   3. an empty table yields nothing, and says so on the first step;
//   4. Reset starts it over, which is what a second `foreach` over the same
//      value relies on.
//
// Pure RAM — no database, no session, no composer settings.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/system/value/valueTable.h"

namespace {

class ValueTableWalk : public ::testing::Test {
protected:
    // 🛑 THE TABLE LIVES IN THE HEAP, HELD BY AN ibValue, and that is not a style choice — it is the
    // only way this object may be owned. It is REFERENCE COUNTED: a row yielded by the walk holds
    // its owner model (`HoldOwnerModel`, tabularModel.h — IncrRef on take, DecrRef on release),
    // because a row whose model has died is not a weak view, it is a crash. A table on the STACK
    // starts at a count of zero, so the first row to take and release it drove the count through
    // zero and ran `delete` over a stack object: heap corruption, an access violation in the test
    // AND in the fixture's destructor (measured 2026-09-09 — `_CrtIsValidHeapPointer` failed first,
    // which is what said it was a free of something never allocated).
    void SetUp() override {
        m_table = new ibValueModelTable();
        m_holder = m_table;   // the ibValue takes the reference the model needs to exist at all

        m_code = m_table->GetColumnCollection()->AddColumn(
            wxT("Code"), ibTypeDescription(), wxT("Code"));
        ASSERT_NE(m_code, nullptr);
    }

    void AddRow(const wxString& code) {
        const long row = m_table->AppendRow();
        const ibDataViewItem item = m_table->GetItem(row);
        ibValueModel::ibComposerNode* node =
            m_table->GetViewData<ibValueModel::ibComposerNode>(item);
        ASSERT_NE(node, nullptr);
        node->SetValue((ibMetaID)m_code->GetColumnID(), ibValue(code));
    }

    // The one cell of a yielded row, read back by the column's own name — the
    // same way a script reads `row.Code`.
    wxString CodeOf(ibValue& row) const {
        ibValue cell;
        const long prop = row.FindProp(wxT("Code"));
        if (prop < 0)
            return wxEmptyString;
        row.GetPropVal(prop, cell);
        return cell.GetString();
    }

    ibValue            m_holder;             // keeps the count above zero for the fixture's life
    ibValueModelTable* m_table = nullptr;    // owned by m_holder, never deleted by hand
    ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo* m_code = nullptr;
};

// 1 + 2 — every row, in storage order, and each one is the row itself.
TEST_F(ValueTableWalk, WalksEveryRowInStorageOrder) {
    AddRow(wxT("a"));
    AddRow(wxT("b"));
    AddRow(wxT("c"));

    const std::shared_ptr<ibValueIteratorState> walk = m_table->CreateIterator();
    ASSERT_TRUE(walk != nullptr);

    std::vector<wxString> seen;
    ibValue row;
    while (walk->MoveNext(row))
        seen.push_back(CodeOf(row));

    ASSERT_EQ(seen.size(), 3u);
    EXPECT_EQ(seen[0], wxT("a"));
    EXPECT_EQ(seen[1], wxT("b"));
    EXPECT_EQ(seen[2], wxT("c"));
}

// 3 — nothing to walk is answered on the first step, not by an empty row.
TEST_F(ValueTableWalk, EmptyTableYieldsNothing) {
    const std::shared_ptr<ibValueIteratorState> walk = m_table->CreateIterator();
    ASSERT_TRUE(walk != nullptr);

    ibValue row;
    EXPECT_FALSE(walk->MoveNext(row));
}

// 4 — a second walk over the same value starts at the beginning. Two `foreach`
// statements over one table is the shape that depends on this.
TEST_F(ValueTableWalk, ResetStartsOver) {
    AddRow(wxT("a"));
    AddRow(wxT("b"));

    const std::shared_ptr<ibValueIteratorState> walk = m_table->CreateIterator();
    ASSERT_TRUE(walk != nullptr);

    ibValue row;
    int first = 0;
    while (walk->MoveNext(row)) first++;
    ASSERT_EQ(first, 2);

    walk->Reset();

    int second = 0;
    while (walk->MoveNext(row)) second++;
    EXPECT_EQ(second, 2);
}

// A row appended after the iterator was made is reached by the SAME walk, because
// the walk holds the model and asks it for the count each step rather than taking
// a copy. This is not a promise about concurrent mutation; it pins that the count
// is not frozen at construction, which is what makes `Reset` above honest.
TEST_F(ValueTableWalk, SeesRowsAppendedBeforeItGetsThere) {
    AddRow(wxT("a"));

    const std::shared_ptr<ibValueIteratorState> walk = m_table->CreateIterator();
    ASSERT_TRUE(walk != nullptr);

    ibValue row;
    ASSERT_TRUE(walk->MoveNext(row));
    EXPECT_EQ(CodeOf(row), wxT("a"));

    AddRow(wxT("b"));

    ASSERT_TRUE(walk->MoveNext(row));
    EXPECT_EQ(CodeOf(row), wxT("b"));
    EXPECT_FALSE(walk->MoveNext(row));
}

} // namespace
