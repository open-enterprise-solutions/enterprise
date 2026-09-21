// =============================================================================
// The value table's column-and-row verbs: Total, FindRows, and Sort.
//
// Code ported from a table-of-values language leans on them all the time - a column's total, the rows
// that match a filter - and the table had neither (card MIG-61 of the migration board, issue #201): a
// script calling `Total` failed with "field not found".
//
// What these pin down is what makes an answer TRUSTWORTHY rather than merely present:
//
//   Total     - exact (0.1 + 0.2 is 0.3, not a double's neighbour); an empty cell adds nothing; a column that
//               is not there, or a cell that is not a number, RAISES - a wrong total that looks right is the
//               worst answer a sum can give;
//   FindRows  - every term must match, in table order; an empty filter is every row; a filter naming a column
//               that is not there raises instead of "no rows";
//   Sort      - ONE column, the second argument its way; `Sort("A")` with one argument works (it used to read a
//               second that was never given); a column that is not there raises and nothing moves. An order
//               over several keys is not parsed out of the string: that is a query's (`orderby A, B`).
//
// Pure RAM: no database, no session, no configuration. Columns are untyped (typing one needs metadata).
// =============================================================================

#include <gtest/gtest.h>

#include "backend/backend_exception.h"
#include "backend/system/value/valueTable.h"

#include <string>
#include <vector>

namespace {

class ValueTableVerbs : public ::testing::Test {
protected:
    // 🛑 HEAP AND HELD - see test_valueTableWalk.cpp: a row takes its owner model and a table on the stack
    // would be deleted through zero.
    void SetUp() override {
        m_table = new ibValueModelTable();
        m_holder = m_table;
    }

    ibValueModelTable::ibValueModelColumnCollection::ibValueModelColumnInfo* Column(const wxString& name) {
        auto* column = m_table->GetColumnCollection()->AddColumn(name, ibTypeDescription(), name);
        EXPECT_NE(column, nullptr);
        return column;
    }

    // One row; `cells` is in the order the columns were added. An Undefined ibValue leaves the cell empty.
    void AddRow(const std::vector<ibValue>& cells) {
        const long row = m_table->AppendRow();
        ibValueModel::ibComposerNode* node = m_table->GetViewData<ibValueModel::ibComposerNode>(m_table->GetItem(row));
        ASSERT_NE(node, nullptr);
        for (size_t i = 0; i < cells.size(); ++i)
            node->SetValue(static_cast<ibMetaID>(m_table->GetColumnCollection()->GetColumnInfo(i)->GetColumnID()), cells[i]);
    }

    // A script's call: the method by its name, the arguments as given.
    ibValue Call(const wxString& method, std::vector<ibValue> args) {
        const long number = m_table->FindMethod(method);
        EXPECT_GE(number, 0) << method.ToStdString();
        std::vector<ibValue*> pointers;
        for (ibValue& arg : args)
            pointers.push_back(&arg);
        ibValue result;
        m_table->CallAsFunc(number, result, pointers.data(), static_cast<long>(pointers.size()));
        return result;
    }

    // The cell of a row a script would read: `row.Name`.
    static wxString Cell(ibValue row, const wxString& column) {
        ibValue cell;
        const long prop = row.FindProp(column);
        if (prop >= 0)
            row.GetPropVal(prop, cell);
        return cell.GetString();
    }

    // The whole table, top to bottom, one column.
    std::vector<wxString> ColumnInOrder(const wxString& column) {
        std::vector<wxString> out;
        const std::shared_ptr<ibValueIteratorState> walk = m_table->CreateIterator();
        ibValue row;
        while (walk != nullptr && walk->MoveNext(row))
            out.push_back(Cell(row, column));
        return out;
    }

    ibValue Text(const wxString& text) { return ibValue(text); }

    ibValue            m_holder;
    ibValueModelTable* m_table = nullptr;
};

wxString Joined(const std::vector<wxString>& items)
{
    wxString out;
    for (const wxString& item : items)
        out += (out.empty() ? wxString() : wxString(wxT(","))) + item;
    return out;
}

} // namespace

// ------------------------------------- Sort -----------------------------------------

TEST_F(ValueTableVerbs, SortWithOneArgument_DoesNotReadASecond)
{
    Column(wxT("N")); Column(wxT("Name"));
    AddRow({ ibValue(3.0), Text(wxT("z")) });
    AddRow({ ibValue(1.0), Text(wxT("x")) });
    AddRow({ ibValue(2.0), Text(wxT("y")) });

    Call(wxT("Sort"), { Text(wxT("N")) });   // ONE argument: the way is not given

    EXPECT_EQ(Joined(ColumnInOrder(wxT("Name"))), wxT("x,y,z"));
}

TEST_F(ValueTableVerbs, SortSecondArgument_IsTheWay)
{
    Column(wxT("N")); Column(wxT("Name"));
    AddRow({ ibValue(1.0), Text(wxT("x")) });
    AddRow({ ibValue(3.0), Text(wxT("z")) });
    AddRow({ ibValue(2.0), Text(wxT("y")) });

    Call(wxT("Sort"), { Text(wxT("N")), ibValue(false) });

    EXPECT_EQ(Joined(ColumnInOrder(wxT("Name"))), wxT("z,y,x"));
}

TEST_F(ValueTableVerbs, SortNamingAMissingColumn_RaisesAndMovesNothing)
{
    Column(wxT("N")); Column(wxT("Name"));
    AddRow({ ibValue(2.0), Text(wxT("b")) });
    AddRow({ ibValue(1.0), Text(wxT("a")) });

    EXPECT_THROW(Call(wxT("Sort"), { Text(wxT("NoSuchColumn")) }), ibBackendException);
    EXPECT_EQ(Joined(ColumnInOrder(wxT("Name"))), wxT("b,a")) << "nothing moves";

    // Nor is a list of keys read out of the string: an order over several is a query's -
    // `from r in t orderby r.N, r.Name`.
    EXPECT_THROW(Call(wxT("Sort"), { Text(wxT("N, Name Desc")) }), ibBackendException);
    EXPECT_EQ(Joined(ColumnInOrder(wxT("Name"))), wxT("b,a"));
}

// ------------------------------------- Total ----------------------------------------

TEST_F(ValueTableVerbs, Total_IsExact_NotADoubleNeighbour)
{
    Column(wxT("Sum"));
    ibNumber tenth, fifth;
    tenth.FromString(wxT("0.1"));
    fifth.FromString(wxT("0.2"));
    AddRow({ ibValue(tenth) });
    AddRow({ ibValue(fifth) });

    const ibValue total = Call(wxT("Total"), { Text(wxT("Sum")) });
    EXPECT_EQ(total.GetNumber().ToString(), wxT("0.3")) << "money is added in exact decimal";
}

TEST_F(ValueTableVerbs, Total_EmptyCellsAddNothing_AndAnEmptyTableIsZero)
{
    Column(wxT("Sum"));
    EXPECT_EQ(Call(wxT("Total"), { Text(wxT("Sum")) }).GetNumber().ToString(), wxT("0"));

    AddRow({ ibValue(5.0) });
    AddRow({ ibValue() });          // an empty cell
    AddRow({ ibValue(7.0) });
    EXPECT_EQ(Call(wxT("Total"), { Text(wxT("Sum")) }).GetNumber().ToString(), wxT("12"));
}

TEST_F(ValueTableVerbs, Total_OfAColumnThatIsNotThere_Raises)
{
    Column(wxT("Sum"));
    EXPECT_THROW(Call(wxT("Total"), { Text(wxT("Typo")) }), ibBackendException);
}

TEST_F(ValueTableVerbs, Total_OverACellThatIsNotANumber_RaisesNamingTheRow)
{
    Column(wxT("Sum"));
    AddRow({ ibValue(1.0) });
    AddRow({ Text(wxT("twelve")) });
    try {
        Call(wxT("Total"), { Text(wxT("Sum")) });
        FAIL() << "a text cell must not be skipped or guessed at";
    }
    catch (const ibBackendException& err) {
        EXPECT_NE(err.GetErrorDescription().Find(wxT("row 2")), wxNOT_FOUND) << err.GetErrorDescription().ToStdString();
        EXPECT_NE(err.GetErrorDescription().Find(wxT("Sum")), wxNOT_FOUND);
    }
}

// ----------------------------------- FindRows ---------------------------------------

namespace {
ibValue MakeFilter(const std::vector<std::pair<wxString, ibValue>>& terms)
{
    ibValueStructure* filter = new ibValueStructure();
    ibValue holder;
    holder = filter;
    for (const auto& term : terms)
        filter->Insert(ibValue(term.first), term.second);
    return holder;
}
}

TEST_F(ValueTableVerbs, FindRows_EveryTermMustMatch_InTableOrder)
{
    Column(wxT("Priority")); Column(wxT("Level")); Column(wxT("Name"));
    AddRow({ ibValue(1.0), ibValue(1.0), Text(wxT("a")) });
    AddRow({ ibValue(1.0), ibValue(2.0), Text(wxT("b")) });
    AddRow({ ibValue(2.0), ibValue(1.0), Text(wxT("c")) });
    AddRow({ ibValue(1.0), ibValue(1.0), Text(wxT("d")) });

    const ibValue found = Call(wxT("FindRows"), { MakeFilter({ { wxT("Priority"), ibValue(1.0) }, { wxT("Level"), ibValue(1.0) } }) });
    ibValueArray* rows = found.ConvertToType<ibValueArray>();
    ASSERT_NE(rows, nullptr);
    ASSERT_EQ(rows->Count(), 2u);

    ibValue first, second;
    rows->GetAt(ibValue(0.0), first);
    rows->GetAt(ibValue(1.0), second);
    EXPECT_EQ(Cell(first, wxT("Name")), wxT("a"));
    EXPECT_EQ(Cell(second, wxT("Name")), wxT("d"));
}

TEST_F(ValueTableVerbs, FindRows_NoMatch_IsAnEmptyArray)
{
    Column(wxT("Code")); AddRow({ Text(wxT("A")) });
    const ibValue found = Call(wxT("FindRows"), { MakeFilter({ { wxT("Code"), Text(wxT("Z")) } }) });
    ibValueArray* rows = found.ConvertToType<ibValueArray>();
    ASSERT_NE(rows, nullptr);
    EXPECT_EQ(rows->Count(), 0u);
}

TEST_F(ValueTableVerbs, FindRows_EmptyFilter_IsEveryRow)
{
    Column(wxT("Code"));
    AddRow({ Text(wxT("A")) });
    AddRow({ Text(wxT("B")) });
    const ibValue found = Call(wxT("FindRows"), { MakeFilter({}) });
    ibValueArray* rows = found.ConvertToType<ibValueArray>();
    ASSERT_NE(rows, nullptr);
    EXPECT_EQ(rows->Count(), 2u);
}

TEST_F(ValueTableVerbs, FindRows_NamingAColumnThatIsNotThere_Raises_NotNoRows)
{
    Column(wxT("Code")); AddRow({ Text(wxT("A")) });
    EXPECT_THROW(Call(wxT("FindRows"), { MakeFilter({ { wxT("Kode"), Text(wxT("A")) } }) }), ibBackendException)
        << "a typo in a filter must not read as \"no such rows\"";
}

TEST_F(ValueTableVerbs, FindRows_WithoutAStructure_Raises)
{
    Column(wxT("Code")); AddRow({ Text(wxT("A")) });
    EXPECT_THROW(Call(wxT("FindRows"), { Text(wxT("Code")) }), ibBackendException);
}

// The rows come back as the table's OWN rows: changing one changes the table.
TEST_F(ValueTableVerbs, FindRows_ReturnsTheTablesOwnRows)
{
    Column(wxT("Code")); Column(wxT("Note"));
    AddRow({ Text(wxT("A")), Text(wxT("old")) });
    AddRow({ Text(wxT("B")), Text(wxT("old")) });

    const ibValue found = Call(wxT("FindRows"), { MakeFilter({ { wxT("Code"), Text(wxT("B")) } }) });
    ibValueArray* rows = found.ConvertToType<ibValueArray>();
    ASSERT_NE(rows, nullptr);
    ASSERT_EQ(rows->Count(), 1u);
    ibValue row;
    rows->GetAt(ibValue(0.0), row);
    const long prop = row.FindProp(wxT("Note"));
    ASSERT_GE(prop, 0);
    row.SetPropVal(prop, Text(wxT("new")));

    EXPECT_EQ(Joined(ColumnInOrder(wxT("Note"))), wxT("old,new"));
}

// A term on an INDEXED column is answered by that column's index, as Find is - and the answer is the scan's:
// the same rows in table order, the other terms still thinning them.
TEST_F(ValueTableVerbs, FindRows_OnAnIndexedColumn_AnswersAsTheScanDoes)
{
    auto* priority = Column(wxT("Priority")); Column(wxT("Level")); Column(wxT("Name"));
    AddRow({ ibValue(1.0), ibValue(1.0), Text(wxT("a")) });
    AddRow({ ibValue(1.0), ibValue(2.0), Text(wxT("b")) });
    AddRow({ ibValue(2.0), ibValue(1.0), Text(wxT("c")) });
    AddRow({ ibValue(1.0), ibValue(1.0), Text(wxT("d")) });

    const ibValue filter = MakeFilter({ { wxT("Priority"), ibValue(1.0) }, { wxT("Level"), ibValue(1.0) } });
    const auto namesFound = [&]() {
        std::vector<wxString> names;
        const ibValue found = Call(wxT("FindRows"), { filter });
        ibValueArray* const rows = found.ConvertToType<ibValueArray>();
        if (rows == nullptr)
            return names;
        for (unsigned int i = 0; i < rows->Count(); ++i) {
            ibValue row;
            rows->GetAt(ibValue(static_cast<double>(i)), row);
            names.push_back(Cell(row, wxT("Name")));
        }
        return names;
    };

    const std::vector<wxString> scanned = namesFound();
    priority->SetColumnIndexed(true);
    const std::vector<wxString> indexed = namesFound();

    EXPECT_EQ(Joined(scanned), wxT("a,d"));
    EXPECT_EQ(Joined(indexed), Joined(scanned)) << "the index answers what the scan answered";
}

// The surface a script and the syntax helper see.
TEST_F(ValueTableVerbs, TheTable_ExposesTheNewVerbs)
{
    EXPECT_GE(m_table->FindMethod(wxT("Total")), 0);
    EXPECT_GE(m_table->FindMethod(wxT("FindRows")), 0);
    EXPECT_GE(m_table->FindMethod(wxT("Sort")), 0);
}

// A column added with no type holds a string of ANY length: it carried the designer's default of ten
// characters, and a cell kept the first ten of whatever was written into it (2026-09-21).
TEST_F(ValueTableVerbs, AColumnAddedWithoutAType_KeepsTextOfAnyLength)
{
    ibValueModelTable::ibValueModelColumnCollection* const columns = m_table->GetColumnCollection();
    const long add = columns->FindMethod(wxT("AddColumn"));
    ASSERT_GE(add, 0);
    ibValue name = Text(wxT("Note")), made;
    ibValue* args[] = { &name };
    ASSERT_TRUE(columns->CallAsFunc(add, made, args, 1));

    ibValue row = Call(wxT("Add"), {});
    const long prop = row.FindProp(wxT("Note"));
    ASSERT_GE(prop, 0);
    const wxString text = wxT("abcdefghijklmnopqrstuvwxyz");
    row.SetPropVal(prop, Text(text));
    EXPECT_EQ(Cell(row, wxT("Note")), text);
}
