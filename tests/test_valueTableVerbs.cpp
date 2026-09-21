// =============================================================================
// The value table's column-and-row verbs: Total, FindRows, and Sort on several keys.
//
// Code ported from a table-of-values language leans on them all the time - a column's total, the rows
// that match a filter, `Sort("Priority, Level Desc")` - and the table had none of the three (card MIG-61
// of the migration board, issue #201): a script calling `Total` failed with "field not found".
//
// What these pin down is what makes an answer TRUSTWORTHY rather than merely present:
//
//   Total     - exact (0.1 + 0.2 is 0.3, not a double's neighbour); an empty cell adds nothing; a column that
//               is not there, or a cell that is not a number, RAISES - a wrong total that looks right is the
//               worst answer a sum can give;
//   FindRows  - every term must match, in table order; an empty filter is every row; a filter naming a column
//               that is not there raises instead of "no rows";
//   Sort      - several keys, each with its own way, later keys breaking only the ties of earlier ones (rows
//               equal on every key keep their order); nothing moves when a named column is missing; and
//               `Sort("A")` with ONE argument works (it used to read a second that was never given).
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

// --------------------------------- ParseSortSpec -----------------------------------

TEST(SortSpec, OneColumn_TakesTheDefaultWay)
{
    std::vector<std::pair<wxString, bool>> keys;
    wxString bad;
    ASSERT_TRUE(ibValueModelTable::ParseSortSpec(wxT("Code"), true, keys, bad));
    ASSERT_EQ(keys.size(), 1u);
    EXPECT_EQ(keys[0].first, wxT("Code"));
    EXPECT_TRUE(keys[0].second);

    ASSERT_TRUE(ibValueModelTable::ParseSortSpec(wxT("Code"), false, keys, bad));
    EXPECT_FALSE(keys[0].second) << "the second argument of Sort() is the way for a key that names none";
}

TEST(SortSpec, SeveralColumns_EachWithItsOwnWay)
{
    std::vector<std::pair<wxString, bool>> keys;
    wxString bad;
    ASSERT_TRUE(ibValueModelTable::ParseSortSpec(wxT("Priority, Level Desc"), true, keys, bad));
    ASSERT_EQ(keys.size(), 2u);
    EXPECT_EQ(keys[0].first, wxT("Priority"));
    EXPECT_TRUE(keys[0].second);
    EXPECT_EQ(keys[1].first, wxT("Level"));
    EXPECT_FALSE(keys[1].second);
}

TEST(SortSpec, SpacingAndCaseOfTheWayAreForgiven)
{
    std::vector<std::pair<wxString, bool>> keys;
    wxString bad;
    ASSERT_TRUE(ibValueModelTable::ParseSortSpec(wxT("  A   desc ,B ASC , C  Descending,D ascending "), false, keys, bad));
    ASSERT_EQ(keys.size(), 4u);
    EXPECT_FALSE(keys[0].second);
    EXPECT_TRUE(keys[1].second);
    EXPECT_FALSE(keys[2].second);
    EXPECT_TRUE(keys[3].second);
    EXPECT_EQ(keys[3].first, wxT("D"));
}

TEST(SortSpec, AMisreadKey_IsRefused_AndSaidWhich)
{
    std::vector<std::pair<wxString, bool>> keys;
    wxString bad;

    EXPECT_FALSE(ibValueModelTable::ParseSortSpec(wxT("A Sideways"), true, keys, bad));
    EXPECT_EQ(bad, wxT("A Sideways")) << "a misread direction must not become a silent ascending sort";

    EXPECT_FALSE(ibValueModelTable::ParseSortSpec(wxT("A B C"), true, keys, bad));
    EXPECT_FALSE(ibValueModelTable::ParseSortSpec(wxT("A,,B"), true, keys, bad)) << "an empty key between commas";
    EXPECT_FALSE(ibValueModelTable::ParseSortSpec(wxT(""), true, keys, bad)) << "nothing to sort by";
    EXPECT_FALSE(ibValueModelTable::ParseSortSpec(wxT("A,"), true, keys, bad)) << "a trailing comma leaves an empty key";
}

// ------------------------------------- Sort -----------------------------------------

TEST_F(ValueTableVerbs, SortOnSeveralKeys_LaterKeysBreakTiesOnly)
{
    Column(wxT("Priority")); Column(wxT("Level")); Column(wxT("Name"));
    AddRow({ ibValue(2.0), ibValue(1.0), Text(wxT("c")) });
    AddRow({ ibValue(1.0), ibValue(2.0), Text(wxT("b")) });
    AddRow({ ibValue(1.0), ibValue(1.0), Text(wxT("a")) });
    AddRow({ ibValue(2.0), ibValue(2.0), Text(wxT("d")) });
    AddRow({ ibValue(1.0), ibValue(1.0), Text(wxT("e")) });

    Call(wxT("Sort"), { Text(wxT("Priority, Level Desc")) });

    // Priority ascending; inside it Level descending; a and e are equal on both keys and keep their order.
    EXPECT_EQ(Joined(ColumnInOrder(wxT("Name"))), wxT("b,a,e,d,c"));
}

TEST_F(ValueTableVerbs, SortWithOneArgument_DoesNotReadASecond)
{
    Column(wxT("N")); Column(wxT("Name"));
    AddRow({ ibValue(3.0), Text(wxT("z")) });
    AddRow({ ibValue(1.0), Text(wxT("x")) });
    AddRow({ ibValue(2.0), Text(wxT("y")) });

    Call(wxT("Sort"), { Text(wxT("N")) });   // ONE argument: the way is not given

    EXPECT_EQ(Joined(ColumnInOrder(wxT("Name"))), wxT("x,y,z"));
}

TEST_F(ValueTableVerbs, SortSecondArgument_IsTheWayForAKeyThatNamesNone)
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

    EXPECT_THROW(Call(wxT("Sort"), { Text(wxT("N, NoSuchColumn")) }), ibBackendException);
    EXPECT_EQ(Joined(ColumnInOrder(wxT("Name"))), wxT("b,a")) << "the first key must not have been carried out";
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
