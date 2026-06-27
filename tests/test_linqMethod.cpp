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
