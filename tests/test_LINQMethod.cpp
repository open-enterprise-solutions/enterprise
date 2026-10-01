// =============================================================================
// OES Enterprise — LINQ method-table tests
//
// ibValue::GetLinqMethodTable() is the SINGLE source of truth that services both
// the compile-side resolver (FindLinqMethodByName: chain method name -> enum, to
// choose OPER_CALL_LINQ vs OPER_CALL_METHOD) and frontend autocomplete. If a name
// and its enum id drift apart, the compiler emits the wrong opcode and the LINQ
// op silently dispatches to the wrong case. These pins guard that lock-step.
// =============================================================================

#include <gtest/gtest.h>
#include "backend/compiler/value.h"   // ibValue::FindLinqMethodByName / GetLinqMethodTable / ibLinqMethod

TEST(LinqMethod, ResolvesKnownNames) {
    EXPECT_EQ(ibValue::FindLinqMethodByName(wxT("Where")),   (long)ibValue::ibLinqMethod::Where);
    EXPECT_EQ(ibValue::FindLinqMethodByName(wxT("Select")),  (long)ibValue::ibLinqMethod::Select);
    EXPECT_EQ(ibValue::FindLinqMethodByName(wxT("GroupBy")), (long)ibValue::ibLinqMethod::GroupBy);
    EXPECT_EQ(ibValue::FindLinqMethodByName(wxT("ToArray")), (long)ibValue::ibLinqMethod::ToArray);
}

TEST(LinqMethod, CaseInsensitive) {
    const long ref = ibValue::FindLinqMethodByName(wxT("Where"));
    EXPECT_EQ(ibValue::FindLinqMethodByName(wxT("where")), ref);
    EXPECT_EQ(ibValue::FindLinqMethodByName(wxT("WHERE")), ref);
}

TEST(LinqMethod, UnknownReturnsMinusOne) {
    EXPECT_EQ(ibValue::FindLinqMethodByName(wxT("NotALinqMethod")), -1);
    EXPECT_EQ(ibValue::FindLinqMethodByName(wxEmptyString), -1);
}

TEST(LinqMethod, TableIsNonEmpty) {
    EXPECT_FALSE(ibValue::GetLinqMethodTable().empty());
}

// Lock-step guard: every table entry's NAME must resolve to its OWN enum id.
// Catches a name typo, a duplicate, or an enum/table reordering.
TEST(LinqMethod, EveryTableEntryNameResolvesToItsId) {
    for (const ibValue::ibLinqMethodInfo& e : ibValue::GetLinqMethodTable()) {
        EXPECT_EQ(ibValue::FindLinqMethodByName(wxString(e.name)), (long)e.id)
            << "method name: " << wxString(e.name).ToStdString();
    }
}

// ...AND THE OTHER DIRECTION, which the test above cannot see. It walks the
// TABLE, so an enum value added without its row is invisible to it — and that
// failure is silent in the worst way: the compiler never resolves the name, so
// the call emits OPER_CALL_METHOD, the receiver has no such method, and the user
// reads "field not found" about a method the language does list.
//
// Counting is enough because the enum is contiguous and Average is last by
// construction (values are appended — an AOT-compiled module carries these
// numbers on disk).
TEST(LinqMethod, TableCoversEveryEnumValue) {
    const size_t declared = (size_t)ibValue::ibLinqMethod::Average + 1;
    EXPECT_EQ(ibValue::GetLinqMethodTable().size(), declared)
        << "an ibLinqMethod value has no row in GetLinqMethodTable(), so its name "
           "will never resolve and the call will emit the wrong opcode";
}

// The aggregates are pipeline operations, not merely Array methods — this is the
// pin for the gap that made `arr.Where(...).Sum()` fail while `arr.Sum()` worked.
TEST(LinqMethod, AggregatesResolveAsPipelineOps) {
    EXPECT_EQ(ibValue::FindLinqMethodByName(wxT("Sum")),     (long)ibValue::ibLinqMethod::Sum);
    EXPECT_EQ(ibValue::FindLinqMethodByName(wxT("Min")),     (long)ibValue::ibLinqMethod::Min);
    EXPECT_EQ(ibValue::FindLinqMethodByName(wxT("Max")),     (long)ibValue::ibLinqMethod::Max);
    EXPECT_EQ(ibValue::FindLinqMethodByName(wxT("Average")), (long)ibValue::ibLinqMethod::Average);
}
